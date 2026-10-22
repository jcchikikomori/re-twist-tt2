# Welcome flow: splash → loading → main menu

Runtime behaviour of the three scenes shown before gameplay, recovered from
`TextTwist2.exe` (installed build, SHA-256 `0c6e962c…`, image base
`0x400000`; see `binary-fingerprint.md` §0) and from the install's
`properties/*.xml`. Everything marked **CONFIRMED** was read from the
decompiled code; **XML** comes straight from the data files; **INFERRED**
is a reading of the GF2 engine's attribute semantics that the code does
not spell out.

Legal note: this file documents timings, layout and asset *IDs* only.
No art, audio or font data from the install is reproduced here or in the
repository.

## 0. Engine facts that apply to every scene

- Window and renderer are 800×600 (`application.xml`, `gamewindow` /
  `renderer`). Scenes are created from `properties/dialogs.xml`
  blueprints by name (`createscenes`), then `activatescene` starts
  `SCENE_SPLASH`.
- Sections `splash`, `music`, `preloading` are **preloaded** before the
  first scene; `default` and `sceneselection` are loaded in the
  background while the splash/loading scenes run (`preloadsections` vs
  `loadsections`).
- Scene classes are registered by a static factory per class name
  (`"SceneSplash"` at `0x56b400`, `"LoadScene"` at `0x56b2b0`). Each
  scene object is a `GF2::Scene` with a modifier queue; the GF2 modifiers
  used here are:

  | Modifier | Ctor | Meaning | Timing arguments |
  | --- | --- | --- | --- |
  | `ModifierDelay` | `0x450b10` | wait N ms | `EDX` = ms |
  | `ModifierAlpha` | `0x451030` | fade alpha from→to | `EDX` = ms; from / to on the stack |
  | `ModifierFrame` | `0x4793c0` (all frames) / `0x479420` (range) | step sprite frames | first stack argument = **ms per frame** (`+0x48`; 0 = set the frame at once); `EDX` = **pass count** (`+0x44`; 1 = once, ≤ 0 = loop) |
  | `ModifierMethod<T>` | `0x42c030`, `0x4643c0` | call a member function when reached | none |

  Modifiers are chained on a target with `0x4506b0` (append to the
  target's queue). A queue runs sequentially, so "delay → method" means
  the method fires after the delay. **CONFIRMED**
- The `ModifierFrame` row follows `game-scene.md` §9.1 (AN1, AN3): its
  update `0x479710` steps the frame each time the elapsed time reaches
  `+0x48`, and at the last frame ends, counts `+0x44` down, or loops.
  An older version of this table called `EDX` the ms per frame. The
  frame times in the flows below (`LOGO` 100 ms, `LOGO_COIL` 70 ms,
  `LOGO_BOUNCE` 60 ms per frame) already are the stack argument, as
  `game-scene.md` §9.1 notes, so none of them changes. **CONFIRMED**
- `FadeBlackTransition`: `TRANSITION_DEFAULT` = 500 ms, `TRANSITION_SPLASH`
  = 500 ms with `wait="false"` (the next scene starts loading while the
  fade runs). **XML**
- Each half of the fade takes the **full** `time`, so a 500 ms transition
  is 500 ms to black plus 500 ms back in (1 s in all), not 250 + 250.
  The XML parser `0x46bd90` stores `time` at `+0x21c` (ctor `0x46bbf0`
  defaults it to 1000). The fade out `0x46c2f0` builds a `ModifierAlpha`
  0 → 1 at `0x46c362` and the fade in `0x46c740` builds 1 → 0 at
  `0x46c78f`; both pass `[esi+0x21c]` as the duration. **CONFIRMED**

## 1. SceneSplash (`SCENE_SPLASH`, section `splash`)

What the user sees: the publisher card, `images/splash/background.jpg`
(800×600, no alpha): the GameHouse logo on a black field. No game art.

### Construction defaults (`0x42c180`) — CONFIRMED

| Field | Default | Meaning |
| --- | --- | --- |
| `+0x1e4` (`[0x79]`) | `1` | `SHOW_GAMEHOUSE_ANIMATION` |
| `+0x1e8` (`[0x7a]`) | `1700` | `SPLASH_DISPLAY_TIME` ms |
| `+0x1ec` (`[0x7b]`) | `500` | `SPLASH_FADE_TIME` ms |
| `+0x1e5` | `0` | "splash sequence finished" |
| `+0x1e6` | `0` | "transition already requested" |
| `+0x1f0` (`[0x7c]`) | `0` | index of the next `splash/splash<N>` image |

### Init (`0x42c1f0`) — CONFIRMED

1. Creates an 800×600 `GF2::Sprite` with no image (`0x442be0`) and adds
   it as a scene component (`[0x7d]`). It is the black cover used to fade
   between splash cards.
1. If `properties/splash/branding.xml` exists it overrides
   `SPLASH_FADE_TIME`, `SPLASH_DISPLAY_TIME` and
   `SHOW_GAMEHOUSE_ANIMATION`. **The installed game has no such file**, so
   the defaults above apply.
1. Calls the sequence step `0x42c760` with index 0.

### Sequence step (`0x42c760`) — CONFIRMED

Each call handles one card, then re-queues itself through a
`ModifierMethod` when there is more to show.

```text
img = file-exists("splash/splash" + idx + ext)   // two extensions tried
if SHOW_GAMEHOUSE_ANIMATION:                     // first call only
    cover.alpha = 0
    scene ← ModifierDelay(SPLASH_DISPLAY_TIME + 400)   // 1700 + 400 = 2100 ms
    if img: cover ← ModifierAlpha(0 → 1, SPLASH_FADE_TIME)
if img:
    sprite centred: x = (800 − w) / 2, y = (600 − h) / 2, alpha = 0
    sprite ← Alpha(0→1, 500) → Delay(1700) → Alpha(1→0, 500)
    idx += 1
finished = (not SHOW_GAMEHOUSE_ANIMATION) and (not img)
SHOW_GAMEHOUSE_ANIMATION = 0
if not finished: scene ← ModifierMethod(0x42c760)   // next card
```

The installed game ships **no** `splash/splash0.*` images, so the only
card is the background itself, held for **2100 ms**.

### Update (`0x42ce20`) — CONFIRMED

Once `finished` is set and the `preloading` section has finished
loading (`0x44c150` returns 0), the scene plays `TRANSITION_SPLASH`
(500 ms fade to black, non-blocking) and pushes `SCENE_LOAD`. There is
no click-to-skip; input is ignored.

Resulting timeline on the shipped data: GameHouse card ≥ 2100 ms (longer
only if preloading is still busy) → 500 ms fade to black → loading
scene.

## 2. LoadScene (`SCENE_LOAD`, section `preloading`)

### Layout (`dialogs.xml`) — XML

Background `images/loadscene/background.png` (800×600 radial blue).
Components, in draw order:

| Name | Image (sheet) | Position | Notes |
| --- | --- | --- | --- |
| `CIRCLES_BRIGHT` | `ANI_CIRCLES` 6000×600, 15 cols × 2 rows = 30 frames of 400×300 | 400,300 | `flip="vertical"`, `frametime="100"`, `currentframe="19"` |
| `CIRCLES_BLEFT` | same | 0,300 | `pivotpoint="bottomleft"`, `flip="both"`, `frametime="100"` |
| `CIRCLES_LEFT` | same | 0,0 | `flip="horizontal"`, `frametime="100"` |
| `CIRCLES_RIGHT` | same | 400,0 | `frametime="100"`, `currentframe="22"` |
| `SPRING` | `IMAGE_SPRING` 800×600 | 0,0 | static overlay |
| `LOADING_BAR_EMPTY` | `IMAGE_LOADINGBAR_EMPTY` 483×69 | 159,445 | seven empty tiles |
| `PROGRESS_BAR` | `LOADING_BAR` 483×69 | 159,445 | "LOADING" tiles, clipped by progress |
| `CLICK_TEXT` | `CLICK_HERE` 400×20 | 320,440 | `anchorpoint="top"`, `visible="false"` — **never shown in this build** |
| `LOGO` | `IMAGE_LOGO_ANIM` 5700×864, 12 cols × 2 rows = 24 frames of 475×432 | 161,0 | the Text Twist 2 logo drop animation |
| `GH_BRANDING` | `IMAGE_GH_BRANDING` 313×70 | 8,521 | small publisher mark |

The four `ANI_CIRCLES` sprites are the four 400×300 quadrants of the
screen, each playing the same 30-frame ripple loop at 100 ms/frame
(3 s per loop) with a flip per quadrant and two of them phase-shifted
(`currentframe` 19 and 22). **XML**; the auto-play meaning of
`frametime` is **INFERRED** (the scene code never touches them).

### Construction defaults (`0x42af80`) — CONFIRMED

`+0x1e4` elapsed ms = 0; `+0x1e8` finished = 0; `+0x1ec` first progress
sample = −1.0; `+0x1f0` progress sprite = null.

### Init (`0x42b080`) — CONFIRMED

1. Starts stream `MUSIC_game_load` (`music/music_game_load.ogg`) unless
   that stream is already the current one.
1. Unloads section `splash` and unregisters `SCENE_SPLASH`.
1. Reads `properties/partners.xml` if present (not in the install).
1. Looks up `PROGRESS_BAR` and installs a clip rect of **zero width**
   (full height), so the bar starts empty. Marks the section as
   "loading in progress".
1. Creates the five high-score tables (`timed`, `untimed`, `blitz`,
   `crossword`, `wotd`; 10 entries each) if they do not exist yet.
1. Queues on `LOGO`: `ModifierDelay(500)` → `ModifierFrame(all frames,
   100 ms/frame)`. With 24 frames the logo animation runs from
   t = 0.5 s to t = 2.9 s.
1. Plays sound `MUSIC_LOGO` (`music/music_logo_animation.ogg`, from the
   `default` section) at volume 1.0.

### Update (`0x42bd70`, called with `dt` ms) — CONFIRMED

```text
if finished: return
elapsed += dt
loader.tick()
if loader.done and nothing pending:
    PROGRESS_BAR.clip = none            // bar fully lit
    if elapsed < 3500: return           // minimum hold 3.5 s
    finished = 1
    clear "loading in progress"
    scene ← ModifierMethod(0x42bf30)   // next frame: go to menu
else:
    Sleep(100)                          // yes, the original sleeps the thread
    p = loader.progress()               // fraction of the pending load
    if first < 0: first = p             // remember the first sample
    PROGRESS_BAR.clip = (0, 0, 483 * (p − first) / (1 − first), 69)
```

`0x42bf30` plays `TRANSITION_DEFAULT` (500 ms fade to black) and pushes
`SCENE_SELECTION`. There is no "click here" step: the scene advances on
its own as soon as both conditions hold (assets loaded, ≥ 3500 ms shown).
The progress value is the background load of the `default` +
`sceneselection` sections, so on a modern machine the bar fills almost
immediately and the scene is held by the 3.5 s floor.

**In the port** (`ui/include/tt2ui/boot_preload.hpp`): the same split.
The loading screen's files (what `LoadScene` draws, `MUSIC_game_load`,
`MUSIC_LOGO`) are decoded on worker threads during the splash, which
waits for them; the menu's (what `SelectionScene` draws and plays, and
the fonts drawn over it) load during the loading screen, and the bar
shows their progress. Both lists are taken from the scenes themselves.
An earlier port loaded each file at its first draw: the loading
screen's first frame then decoded `ANI_CIRCLES` and `IMAGE_LOGO_ANIM`
(~150 ms on a desktop, 1–2 s on a low-end tablet), and with the step
clamped to 100 ms the logo animation fell that far behind `MUSIC_LOGO`.

## 3. SelectionScene (`SCENE_SELECTION`, section `sceneselection`)

### Layout (`dialogs.xml`) — XML

Background `images/selectionscene/background.png`. Sprite sheets with
`rowcount="4"` or `framecount="4"` hold the four button states
(normal / hover / pressed / disabled), one per row.

| Name | Image | Position | Notes |
| --- | --- | --- | --- |
| `LABEL_PLAYERNAME` | font `FONT_FONT_UIBAR` (`fonts/font_uibar.xml`) | 400,28 | `anchorpoint="center"`, width 280, `charspacing="2"`, centred, shows the current player name (placeholder text `MUGWUMP`) |
| `BUTTON_CHANGENAME` | `change_name.png` 155×52 ×4 | 324,49 | tooltip `CTOOLTIP_SELECTION_CHANGE_NAME` |
| `BUTTON_PLAY_UNTIMED` | `button_untimed.png` 160×536 ×4 | 200,290 | `hitmaskthreshold="0"` |
| `BUTTON_PLAY` (timed) | `button_timed.png` 134×536 ×4 | 451,290 | `hitmaskthreshold="75"` |
| `BUTTON_PLAY_WOD` | `button_wotd.png` 157×596 ×4 | 85,378 | `disabledsound="SOUND_button_wordoftheday"` |
| `BUTTON_BLITZ` | `button_lightning.png` 170×536 ×4 | 320,393 | |
| `BUTTON_CRISSCROSS` | `button_crossword.png` 206×536 ×4 | 528,393 | |
| `BUTTON_GH` (URL) | `button_gh.png` 115×400 ×4 | 653,210 | opens gamehouse.com (see below) |
| `BUTTON_SETTINGS` | `button_settings.png` 157×208 ×4 | 3,539 | bottom bar, `hitmaskthreshold="100"` |
| `BUTTON_HOWTOPLAY` | `button_howtoplay.png` | 163,539 | |
| `BUTTON_HIGHSCORES` | `button_highscores.png` | 323,539 | |
| `BUTTON_TROPHY` | `button_trophies.png` | 482,539 | |
| `BUTTON_QUIT` | `button_quit.png` | 641,539 | |
| `LOGO_COIL` | `logo_coil.png` 3298×253, 17 frames of 194×253 | 128,64 | spring compress loop |
| `LOGO_BOUNCE` | `logo_bounce.png` 1728×252, 12 frames of 144×252 | 513,43 | ball bounce loop |
| `LOGO_STATIC` | `logo_static.png` 254×204 | 288,84 | the "TEXT TWIST" wordmark |

All buttons use `clicksound="SOUND_BUTTON"`. **Hover plays no sound** (CONFIRMED,
REA batch A, 2026-10-14):

| Fact | Evidence |
| --- | --- |
| The GF2 button parser `0x48d390` reads `clicksound`, `oversound`, `disabledsound`, `soundsection`, `firekey`; a hover sound is set **only** from an `oversound` attribute | `ev_9868528458dbaeab13eb36a04bd7b1af6768b2b2cb5d6673cf84ed1484f9bbf4` |
| The exe has no `SOUND_button_rollover` / `rollover` string; the sound attribute strings are `offsound`, `onsound`, `oversound`, `disabledsound`, `clicksound`, `sound`, `playsound` | `ev_a8e7c64d6cfc29282762153ff8557396cbf854d18d9494f89d144eed89a23be0`, `ev_3469b5d5e980e801ba86e36175624c28cff71477384e94e7d3df1030d80f5831` |
| No element of the install XML sets `oversound` (only `clicksound` ×71, `onsound` ×10, `offsound` ×5, `onlysound` ×2, `disabledsound` ×1); `SOUND_button_rollover` is only declared in `resources.xml` | read-only grep of `properties/*.xml` |

So no button in any scene or dialog plays `SOUND_button_rollover`. `hitmaskthreshold` is the
alpha threshold for hit testing against the sprite's pixels (**INFERRED** from the name).

`BUTTON_GH` is a `GF2::ButtonURL` (CONFIRMED, 2026-10-15): `dialogs.xml` declares
`<ButtonURL name="BUTTON_GH" … url="http://www.gamehouse.com"/>`, and the class's click slot
`0x477b80` (vtable `0x596f84`, slot 3) runs `ShellExecuteA(GetDesktopWindow(), "open", url, NULL,
NULL, SW_SHOW)`, then the button callback. Nothing else happens on click
(`ev_5da37f5f6ba54321e4a1da549681683e4196592bea62bba85d6312b6f0c33ca5`,
`ev_745a890effe2c0cb57dcd84542b12430d2c0f2a3614f35bf9cd7c45f1a842286`). Whether the `moregamesurl` /
`showmoregames` registry values override the URL is UNKNOWN (strings:
`ev_5dff76359aae16ea7d8e064a5367c4a9cd8d835a58184e80c7da4939777cdfac`). The port does not open
this URL. **Port deviation UX-2:** the slot holds the port's own `BUTTON_IMPROVEMENTS` (same 115×100 frame at
(653, 210), 4 states, generated art `images/selectionscene/button_improvements.png` from `data/placeholder/`),
which opens the IMPROVEMENTS dialog (`ui/include/tt2ui/improvements_dialog.hpp`,
[`docs/design/improve-ui-ux.md`](../design/improve-ui-ux.md)).

### Enter (`0x42d9b0`) and init (`0x42d220`) — CONFIRMED

- Enter: starts stream `MUSIC_menu`, loads the section, resets state.
- Init binds the button handlers (play timed/untimed, blitz, crossword,
  word of the day, high scores, settings, how to play, trophies, quit)
  and stores `LOGO_COIL` (`+0x1f0`) and `LOGO_BOUNCE` (`+0x1f4`).
- Two idle timers are armed: `+0x1f8 = rand() + 3000` and
  `+0x1fc = rand() + 3000` ms, where `rand()` is the MSVC CRT
  `rand()` (0..32767).

### Update (`0x42dd30`, `dt` ms) — CONFIRMED

```text
coilTimer   -= dt;  bounceTimer -= dt
if coilTimer <= 0:
    play SOUND_loading_spring
    LOGO_COIL ← ModifierFrame(0 .. 16, 70 ms/frame)        // 1.19 s
    coilTimer = rand() + 7000
if bounceTimer <= 0:
    play SOUND_loading_bounce
    LOGO_BOUNCE ← Frame(0..11, 60 ms/frame) ×2, plus a third pass if rand() > 6
    bounceTimer = rand() + 7000
```

So the spring twangs and the "2" ball bounces independently, each first
within 3–36 s of entering the menu and then every 7–40 s.

### QUIT confirm (`0x40ec20`) — REA, 2026-10-22

- **Binding (CONFIRMED).** Init `0x42d220` binds `BUTTON_QUIT` (string `0x5910c0`) to `0x40ec20`
  (@`0x42d773`-`0x42d78f`; `ev_6f39440af57feb7eeca064d89e3c478d50ca5b8fdd65b739cdcf6140867d3f3a`). That binding is
  `0x40ec20`'s only reference (`ev_0b0153bbc60a8e140cba09c9a0c9df034d287431687ed87371bc12eebbdebbac`), so no key
  opens the quit confirm.
- **Handler `0x40ec20` (CONFIRMED,** `ev_0d44a081ef5abcb2b6b913f6d8a0bd05014dd88872556472fa9d8d24247cb774`**):**
  - It picks the parent: the current scene `[[0x5fde84]+0x28]`, or that scene's `+0x1e8` when a type test
    (`vtable+0x14(0x5fef80)` → `0x45bb00`) passes (INFERRED: a GF2 class check).
  - When the parent already has a child named `CTEXT_QUESTION_QUIT` (`0x45c3b0`, @`0x40eca5`-`0x40ece1`), it does
    nothing, so repeated clicks never stack confirms.
  - It loads resource section `"default"` (`0x44c150`) and polls with `Sleep(1)` until it is ready
    (@`0x40ece7`-`0x40ed70`).
  - It calls `0x40b310(message CTEXT_QUESTION_QUIT, header CTEXT_QUIT_HEADER, receiver [0x5ff648], YES 0x40eb90,
    0, 0)` (@`0x40edea`-`0x40ee02`) and shows the result through vtable `+0xb0` (@`0x40ee5d`-`0x40ee6c`).
- **Dialog (CONFIRMED).** `0x40b310` builds template `DIALOG_YESNO` (string `0x5919b8`,
  `ev_e9c3cf2da01698192c7074845611c86172fa299f9f13312b2d3d6e6cec9cdef3`), named after the message key. It is the
  same factory as "remove player" (`0x40ade0`, `profiles.md`) and the in-game "Exit Game?" (`0x408320`,
  `game-scene.md` §6), so the port's `build_yes_no()` fits. `dialogs.xml:203-210`: autocenter, min 400×180,
  `dodarkenbackground`, movable; `HEADER` (200, 13) `FONT_DIALOG_HEADERS2`; `MESSAGE` (200, 87) width 390,
  wordwrap, `FONT_SETTINGSTEXT`, line height 30; `BUTTON_YES` (190, 100) anchor topright; `BUTTON_NO` (210, 100)
  anchor topleft. `0x40b470` moves YES / NO below the message and grows the height (`game-scene.md` §6). Text:
  `commontext.xml:18-19` "Quit" / "Are you sure you want to quit?".
- **Buttons (CONFIRMED,** `0x40b470` @`0x40b496`-`0x40b624`,
  `ev_b60513ea2c74d452bc58f492ad7395b221cbb5593270adb0597ecc8168719722`**).** Each button gets a (receiver,
  callback) pair. A null receiver becomes the dialog, and a null callback becomes `0x43fcd0`. YES gets
  (`[0x5ff648]`, `0x40eb90`), kept at `+0x1e4`. NO gets (the dialog, `0x43fcd0`), kept at `+0x1e8`.
- **YES quits at once (CONFIRMED for the code; the listener registration is INFERRED).**
  - `0x40eb90` (`ev_f088b6a6b139e272a93a2bacceec6de0be7136f3e7a1c9a72aa3053786c58189`) sends a `GF2::Message`
    with id `0x402` to the app's listeners (`0x46cab0` on `[0x5fdea0]+0x40`). It has no fade, music stop or sound.
  - `0x493f00` clears the byte `[0x5c87e5]` on `0x402`, and the main loop `0x46de50` tests that byte
    (@`0x46de53`). A `.text` scan found the other `0x402` listeners: the scene handlers of Classic (`0x428ba6`),
    Lightning (`0x416a16`, saves the game) and Crossword (`0x41fdf6`).
  - Any shutdown fade after the loop ends: UNCONFIRMED (not traced).
- **NO (CONFIRMED,** `ev_50d17151ac336188fdcdcbe82345f67d619c5226e9e42400e32ca0c14dd16c67`**).** `0x43fcd0`
  calls `0x45c470` with the dialog. That calls its vtable `+0xb4` (INFERRED: hide), erases it from the scene's
  list (`+0x1d4`) and releases it. The menu is otherwise untouched. Whether the 200 ms hide fade shows after the
  erase is UNCONFIRMED.
- **Keys.** Esc and Return do nothing to the confirm: dialogs bind no keys (`help-and-settings.md` §4, a bounded
  negative read on `DialogSettings`; INFERRED that `DialogYesNo` behaves the same). Esc only switches fullscreen to
  windowed (`help-and-settings.md` §3.4).
- **Window close quits without asking (CONFIRMED).** The WndProc `0x483ef0` sends `0x402` directly on `WM_CLOSE`
  (`0x10`, @`0x483f13`-`0x483f68`). It then falls to switch entry 6 = `0x4844a2`, which calls `DefWindowProcA`
  (import `0x5762c0`). Alt+F4 takes the same path through `DefWindowProcA` → `SC_CLOSE` → `WM_CLOSE` (INFERRED;
  the WndProc handles no `WM_SYSKEYDOWN` / `WM_SYSCOMMAND`). Esc on the menu never opens the confirm (CONFIRMED,
  single xref).
- **Sounds and tooltips.** `BUTTON_QUIT`, YES and NO all click `SOUND_BUTTON` (`dialogs.xml:54`, `:207-208`).
  `BUTTON_QUIT`'s tooltip is `CTOOLTIP_SELECTION_QUIT`; the text exists only as `_WIN` "Quit the game and return to
  Windows" / `_MAC` (`commontext.xml:84-85`; the suffix is INFERRED, and the port's `tooltip.hpp:98` already maps
  it). Whether this dialog plays `SOUND_DIALOG_SHOW` depends on its `+0x1d8` flag (UNCONFIRMED, as in
  `game-scene.md` §6). It fades in over 200 ms through the base show.
- **Port.** `BUTTON_QUIT` opens `QuitConfirm` (`ui/include/tt2ui/quit_dialog.hpp`), a `build_yes_no()` dialog
  with the text above and the port's usual 200 ms fade and backdrop. A second QUIT while it is up changes nothing.
  YES clicks `SOUND_BUTTON` and quits at once; NO clicks it and closes the dialog. Closing the window and Alt+F4
  (`SDL_EVENT_QUIT`) still quit without asking. **Port deviation:** Return answers YES and Esc answers NO, as
  in the port's other yes/no dialogs; the original binds no keys here.
- **Port, Esc on the menu.** With no dialog open, Esc never quits and never opens the confirm. On the desktop it
  leaves fullscreen (Borderless or Native) for a window and saves that, as the original does, and does nothing in a
  window (`help-and-settings.md` §3.4). On Android, Back arrives as Esc and puts the app in the background.

Local-listing evidence (no REA id): the QUIT binding, `0x40ec84`-`0x40ee6c`, `0x40b310`, `0x40b470`, `0x43fcd0`,
`0x483f13`-`0x483f68`, `0x493f00`, the jump table at `0x484500` / `0x4844e4`, the `DefWindowProcA` import row, and
the `.text` scan for `0x402`.

## 4. Open items

- Exact `frametime` / `currentframe` / `flip` / `pivotpoint` semantics of
  `GF2::Sprite` (section 2 table) are inferred from attribute names.
- `0x463c70(stream, 0x132)` when starting `MUSIC_game_load`: the `306`
  argument is probably a fade-in in ms; not verified.
- `loader.progress()` (`0x44c330`) is assumed to return 0..1; the
  normalisation against the first sample makes the bar fill from empty
  regardless of the actual range.
