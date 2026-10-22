# Text Twist 2 — Improvising the User Interface & User Experience

> This design document consists of improvements that aren't available on the original game

## Upscaled Graphics

- Currently, the game would run in the maximum native resolution which is `1024x768` (i presume), and there is no implementation to upscale the textures, fonts, etc.
- **To the LLM:** If additional tech stack required, please inform the developer

### Graphics pipeline

- The original game uses DirectX 7/8 (based on the system requirements)
- I want this game to use Vulkan graphics pipeline
- OpenGL is also supported, as the fallback when Vulkan is unavailable
- DirectX is currently being ruled out (for now) because it uses the proprietary graphics technology made by Microsoft Corporation.
- Default graphics pipeline will be the software-based if neither Vulkan nor OpenGL is available, and it will disable graphic enhancements, to ensure performance is solid

## Quality-of-life Improvements

### Separate dialog

- On the main menu, replace the "GameHouse - More Great Games" element with our own button for Quality-of-life improvements
- We are going to have a separate dialog for managing improvements
- On the Existing Settings dialog, we should disable/hide the Fullscreen option, since it will be migrated into our own

### Change Resolution

- In Settings, we should be able to change the resolution, **up to 1080p** (for the meantime).
- The fullscreen modes should be either Borderless or Native Fullscreen.

### Widescreen

- The original game only supports 4:3 screen ratio.
- Use procedural generation, and colour pallete from the original game to fill up the blank/black pixels for the widescreen (initial support should be 16:9 screen ratio).

### Screen Refresh Rate

- The original game only supports 60hz.
- This game should support high refresh rate.

## Corrections and decisions

> Added by Claude on 2026-10-15 from the reverse-engineering pass and the planning answers. The text above is
> the original brief, left as written.

### Corrections to the brief

- **Resolution:** the original runs at **800x600** only, not 1024x768. Its window cannot be resized, and its
  fullscreen switches the display to 800x600 ([`display-and-timing.md`](../reverse-engineering/display-and-timing.md)).
  1080p is therefore a 1.8x scale (1440x1080), with 240 px bars on each side of a 16:9 screen.
- **Graphics API:** the original uses **Direct3D 9**, loaded at run time, not DirectX 7/8. There is no OpenGL or
  software path.
- **Refresh rate:** the original caps at about 62.5 fps (`Sleep` to a 16 ms period) with vsync on, and clamps
  each frame's time step to 100 ms. Its game timers are millisecond deltas, so they do not depend on 60 Hz.

### Decisions (2026-10-15)

| Topic | Decision |
| --- | --- |
| Render stack | Migrate from SDL2 to **SDL3**; try Vulkan, then OpenGL, then software. The software renderer turns the enhancements off. Android tries OpenGL ES, then Vulkan, then software ([android-support.md](android-support.md#as-built-2026-10-20)) |
| Upscaling | Smooth / sharp / pixel-art filtering for the imported art, our own placeholder art regenerated at 2x, and an optional texture upscaler (self-written, off by default) |
| Options file | A new `improvements.txt`, so `settings.txt` stays readable by older builds |
| Dialog | **IMPROVEMENTS**, opened from a new main-menu button in place of "GameHouse - More Great Games" |
| Resolution, window mode | Both live in IMPROVEMENTS; SETTINGS hides its FULLSCREEN switch (main menu and pause menu) |
| Renderer change | Applies on the next launch; OK shows a one-button "Restart needed" notice when the pick differs from what runs. The row stays one line: only Auto names a renderer, the one in use ("Auto (OpenGL)") |
| Borderless | Always the desktop size; the resolution setting applies to windowed and native fullscreen. RESOLUTION shows the desktop size greyed out |
| Dialog rows | Every IMPROVEMENTS row is one line: a row this run can't offer is greyed out with no reason under it, and a fallback goes in the value ("Auto (OpenGL)", "Native (failed)") |
| Side fill | Any left/right bar is filled (16:10 and 21:9 too); 16:9 is the supported target |
| Frame rate | VSync on/off plus a frame limit: Off, 60 (original), 120, 144, 165, 240 |

### Tech stack

| Component | Version | License | Source |
| --- | --- | --- | --- |
| SDL3 | 3.4.18 | zlib | Built from the `release-3.4.18` tarball on Linux and Windows (`TT2_FETCH_SDL3`) |
| SDL3_image | 3.4.8 | zlib | `release-3.4.8`, stb decoders only (PNG, JPEG, BMP) |
| SDL3_mixer | 3.2.4 | zlib | `release-3.2.4`, stb_vorbis and WAV only |

Windows used MSYS2's `sdl3` packages at first; their SDL3_image loads libjpeg at run time, the zip didn't carry it,
and the splash came out black. Since then Windows builds the same tarballs as Linux, and CI decodes
`ui/tests/media/` with each package (`tt2 --probe-media`).
| Vulkan loader | the player's system | Apache 2.0 | Loaded by SDL at run time; never bundled |

### Port deviations

Each row is filled in by the phase that ships it.

| ID | Deviation from the original | Shipped in |
| --- | --- | --- |
| UX-1 | Vulkan / OpenGL / software through SDL3 instead of Direct3D 9 | 0.3.0 (P1, P3) |
| UX-2 | IMPROVEMENTS button in place of the GameHouse link | 0.3.0 (P2) |
| UX-3 | SETTINGS hides its FULLSCREEN switch | 0.3.0 (P2) |
| UX-4 | Display options saved in `improvements.txt` | 0.3.0 (P2) |
| UX-5 | Window sizes up to 1080p; borderless and native fullscreen | 0.3.0 (P5) |
| UX-6 | Side bars filled in the original palette | 0.3.0 (P6) |
| UX-7 | No 60 fps cap by default; vsync or a chosen limit | 0.3.0 (P4) |
| UX-8 | Filtering, 2x placeholder art, full-resolution dialog layers | 0.3.0 (P7 filter and layers; P8 art) |
| UX-9 | Optional texture upscaler (Super-xBR 2x) | 0.3.0 (P9) |
| UX-10 | "Continue the game?" prompt before a saved game, with an IMPROVEMENTS switch (ASK TO RESUME) back to the original's silent resume | 0.4.0 |
| UX-11 | Removing a player deletes their saved games; a new player never inherits a removed one's | 0.4.0 |
| UX-12 | IMPROVEMENTS → DAILY WORD CLOCK: `Original` (default) copies the exe's three clocks (UTC word, local standard-time gate, UTC high-score date); `Local time` puts the word, the gate, the save `DAY`, `player.WOD` and the high-score date on the local calendar day with DST (`improvements.txt` `wotd_day=original\|local`; [wotd-crossword.md](wotd-crossword.md) D-12) | 0.6.0 |

## Open considerations

> Added by Claude on 2026-10-22 at the user's request. These are ideas for later, not plans. The items under
> **User decisions** were answered by the user on 2026-10-22; everything else is still open.

### Game and graphics options in a second modal

**Where this lives.** IMPROVEMENTS is the port's own options dialog (deviation UX-2), opened from the main menu's
IMPROVEMENTS badge. Its model is `ImprovementsDialog` in `ui/include/tt2ui/improvements_dialog.hpp`: a list of
**rows**, each a title, a value and two 36x36 arrow buttons that step through the choices, then OK and CANCEL.
`ui/src/main.cpp` owns the open dialog (`menu_improvements`), feeds it mouse and key events, and on OK applies the
values and saves `improvements.txt` (`commit_improvements`, `main.cpp:2514`).

**Trigger.** The dialog is `96 + rows × 38 + 104` px tall (`improvements_dialog_height`). Desktop has 10 rows:
`96 + 380 + 104 = 580` of the 600 px screen (Android has 8 rows: 504). An 11th row makes 618, so the next port
option forces a layout change.

**Idea.** IMPROVEMENTS shows two buttons, **GAME OPTIONS** and **GRAPHICS OPTIONS**. Each opens a second **modal**
(a dialog that takes every click and key until it closes) over IMPROVEMENTS, holding that group's rows:

| Sub-dialog | Rows | Height |
| --- | --- | --- |
| GAME OPTIONS | ASK TO RESUME, DAILY WORD CLOCK | `96 + 2 × 38 + 104 = 276` px |
| GRAPHICS OPTIONS | DISPLAY MODE, RESOLUTION, WIDESCREEN FILL, TEXTURE FILTER, UPSCALER, RENDERER, VSYNC, FRAME LIMIT | 504 px; Android has no DISPLAY MODE or RESOLUTION, so 6 rows and 428 px |

Each sub-dialog fits 10 rows (580 px) before the same limit comes back: room for 8 more game rows and 2 more
graphics rows.

#### User decisions (2026-10-22)

1. **OK in a sub-dialog saves right away.** It applies and writes its own rows (what `commit_improvements` does
   today), and its CANCEL restores only its own rows. The parent then has nothing left to confirm, so its OK and
   CANCEL would become one **CLOSE** (or BACK) button. To confirm later: the word, and its art. The original
   install has `images/gui/button_close.png` and `button_back.png`, but neither has an image id yet (the table at
   `main.cpp:236-266`), and `data/placeholder/` has no pill for them (`tools/gen_placeholders.py:395-402` lists
   the generated buttons).
1. **The renderer's "Restart needed" notice shows once GRAPHICS OPTIONS closes, unless CANCEL closed it.** Today
   it opens on IMPROVEMENTS' OK when the picked renderer is neither the saved one nor the one running
   (`restart_needed()`, `improvements_dialog.hpp:129-131`; opened at `:153-157`). Under this idea it opens over
   IMPROVEMENTS right after GRAPHICS OPTIONS closes with OK. CANCEL in GRAPHICS OPTIONS shows nothing.
1. **Android back and the keyboard behave as usual.** In today's code that means: Android's back button arrives
   as Escape (`main.cpp:2944-2945`). Escape presses CANCEL and Return presses OK on the open dialog
   (`main.cpp:3100-3105` for IMPROVEMENTS, `:3085-3090` for SETTINGS). The dialog hands that press to its child
   first, so only the top dialog closes: SETTINGS passes it to CREDITS (`settings_dialog.hpp:184-193`), and
   IMPROVEMENTS to its notice (`improvements_dialog.hpp:142-149`). So Escape or back closes a sub-dialog as its
   CANCEL would, Return is its OK, and on the parent Escape or back closes IMPROVEMENTS. With no dialog open,
   back on the menu puts the app in the background (`main.cpp:3136-3140`). To confirm together with decision 1:
   what Return does on a parent that only has CLOSE.

#### Still open

- **Dialog stack and fades.** The pieces exist. `dialog_fade.hpp` fades a dialog in and out over 200 ms
  (`DialogFade`, `:56`) and darkens the screen behind it (the **backdrop**, `darken_color`, `:49`). `main.cpp`
  keeps one `DialogLayer` (a fade plus an off-screen texture) per dialog source (`main.cpp:2322-2323`), and draws
  a child in its own layer above its parent, each with its own backdrop: CREDITS over SETTINGS, and "Restart
  needed" over IMPROVEMENTS (`main.cpp:3461-3471`). A sub-dialog would be one more layer of that kind, so two
  backdrops darken the menu while it is open, as with CREDITS today. The model side would copy SETTINGS and
  CREDITS: the parent holds the child in a `std::optional` (`settings_dialog.hpp:363`, opened at `:221`) and
  routes every press to it while it is open. The in-game pause menu uses the other pattern, `hand_off_layers`
  (`main.cpp:1919`, `dialog_fade.hpp:137`), which keeps the pause menu under SETTINGS without fading it again;
  IMPROVEMENTS only opens from the main menu, so it doesn't need that.
- **Buttons and art.** GAME OPTIONS and GRAPHICS OPTIONS are port-only, so they need generated pills, like
  SETTINGS' IMPORT. "GRAPHICS OPTIONS" is too long for IMPORT's 119x50 pill (`gen_placeholders.py:402`); the
  pause menu's 181x52 pills ("RESUME GAME", `:398-401`) are closer.
- **`--type` keys.** The scripted keys for smoke runs press row arrows by name (`main.cpp:3273-3284`: `<` DISPLAY
  MODE, `{` RENDERER, `%` DAILY WORD CLOCK, `|` OK, `~` CANCEL). With the rows in sub-dialogs, either the parent
  forwards a row's button name to the sub-dialog that holds it, or new keys open each sub-dialog first. CI never
  types into IMPROVEMENTS (the workflows and `scripts/` only type `~`, `|` and words), so only manual runs and
  frame dumps change.
- **Hit targets on Android.** The game draws an 800x600 canvas scaled to a landscape screen. On a phone 1080 px
  tall that is 1.8x, so a 36x36 arrow is about 65 px, roughly 22 dp at 480 dpi: under
  [Android's 48 dp touch-target guidance](https://support.google.com/accessibility/android/answer/7101858). The two
  new buttons would be easy to hit, but the arrows stay as small as today. GAME OPTIONS has room for taller rows,
  yet an arrow's hit box is its 36x36 frame (`MenuButton::contains`, `welcome_flow.hpp:301`), so bigger targets
  need new art or a bigger hit box.
- **Test cost.** The 55 tests in `ui/tests/test_improvements_dialog.cpp` assume one dialog. A split needs either
  one model with a row-group parameter, or a parent model and two children. New tests: each button opens its
  sub-dialog; Escape closes only the top dialog; a sub-dialog's OK commits only its rows and its CANCEL restores
  only them; the restart notice after GRAPHICS OPTIONS' OK, never after its CANCEL; Android's shorter graphics
  list. `main.cpp` (layers, `--type`) has no unit tests, so a frame dump of each dialog would check it.
- **Faithfulness.** The original nests one dialog: SETTINGS' INFO opens CREDITS over it (`0x40c760`). It has no
  options sub-dialogs. IMPROVEMENTS is already a port-only deviation (UX-2), so a nested modal adds a port-only
  screen rather than changing an original one. It would get its own UX row in the table above when built.

#### Alternatives

- **A scrollable modal:** one dialog whose rows scroll. Needs scroll input from the mouse wheel, touch drag and
  keys, and clipping in the renderer, which draws dialogs whole today.
- **Tabs inside one dialog:** two tab buttons (GAME, GRAPHICS) swap the row set in place. No nested modal and one
  OK / CANCEL, but two more port-only buttons, and the taller tab sets the dialog's height.
- **Shrinking the rows further:** 36 px is the floor, because the arrow art is 36x36. 11 rows at 36 px make
  `96 + 396 + 104 = 596`, so it buys exactly one more row.
- **Section headings:** tried on 2026-10-22 (GAME and GRAPHICS labels over 36 px rows, 596 px) and removed at the
  user's request: they read like row titles.
