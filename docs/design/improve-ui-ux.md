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
| Render stack | Migrate from SDL2 to **SDL3**; try Vulkan, then OpenGL, then software. The software renderer turns the enhancements off |
| Upscaling | Smooth / sharp / pixel-art filtering for the imported art, our own placeholder art regenerated at 2x, and an optional texture upscaler (self-written, off by default) |
| Options file | A new `improvements.txt`, so `settings.txt` stays readable by older builds |
| Dialog | **IMPROVEMENTS**, opened from a new main-menu button in place of "GameHouse - More Great Games" |
| Resolution, window mode | Both live in IMPROVEMENTS; SETTINGS hides its FULLSCREEN switch (main menu and pause menu) |
| Renderer change | Applies on the next launch |
| Borderless | Always the desktop size; the resolution setting applies to windowed and native fullscreen |
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
| UX-5 | Window sizes up to 1080p; borderless and native fullscreen | |
| UX-6 | Side bars filled in the original palette | |
| UX-7 | No 60 fps cap by default; vsync or a chosen limit | |
| UX-8 | Filtering, 2x placeholder art, full-resolution dialog layers | |
| UX-9 | Optional texture upscaler | |
