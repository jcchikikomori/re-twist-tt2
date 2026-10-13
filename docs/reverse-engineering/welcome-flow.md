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

  | Modifier | Ctor | Meaning |
  | --- | --- | --- |
  | `ModifierDelay` | `0x450b10` | wait N ms (`EDX` = ms) |
  | `ModifierAlpha` | `0x451030` | fade alpha from→to over `EDX` ms |
  | `ModifierFrame` | `0x4793c0` (all frames) / `0x479420` (range) | step sprite frames, `EDX` = ms per frame |
  | `ModifierMethod<T>` | `0x42c030`, `0x4643c0` | call a member function when reached |

  Modifiers are chained on a target with `0x4506b0` (append to the
  target's queue). A queue runs sequentially, so "delay → method" means
  the method fires after the delay. **CONFIRMED**
- `FadeBlackTransition`: `TRANSITION_DEFAULT` = 500 ms, `TRANSITION_SPLASH`
  = 500 ms with `wait="false"` (the next scene starts loading while the
  fade runs). **XML**

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
| `BUTTON_GH` (URL) | `button_gh.png` 115×400 ×4 | 653,210 | opens gamehouse.com |
| `BUTTON_SETTINGS` | `button_settings.png` 157×208 ×4 | 3,539 | bottom bar, `hitmaskthreshold="100"` |
| `BUTTON_HOWTOPLAY` | `button_howtoplay.png` | 163,539 | |
| `BUTTON_HIGHSCORES` | `button_highscores.png` | 323,539 | |
| `BUTTON_TROPHY` | `button_trophies.png` | 482,539 | |
| `BUTTON_QUIT` | `button_quit.png` | 641,539 | |
| `LOGO_COIL` | `logo_coil.png` 3298×253, 17 frames of 194×253 | 128,64 | spring compress loop |
| `LOGO_BOUNCE` | `logo_bounce.png` 1728×252, 12 frames of 144×252 | 513,43 | ball bounce loop |
| `LOGO_STATIC` | `logo_static.png` 254×204 | 288,84 | the "TEXT TWIST" wordmark |

All buttons use `clicksound="SOUND_BUTTON"`; hover uses the engine-wide
`SOUND_button_rollover`. `hitmaskthreshold` is the alpha threshold for
hit testing against the sprite's pixels (**INFERRED** from the name).

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

## 4. Open items

- Exact `frametime` / `currentframe` / `flip` / `pivotpoint` semantics of
  `GF2::Sprite` (section 2 table) are inferred from attribute names.
- `0x463c70(stream, 0x132)` when starting `MUSIC_game_load`: the `306`
  argument is probably a fade-in in ms; not verified.
- `loader.progress()` (`0x44c330`) is assumed to return 0..1; the
  normalisation against the first sample makes the bar fill from empty
  regardless of the actual range.
