# Display, renderer and frame timing

Evidence for how the original sizes its window, renders, goes fullscreen and paces frames. Target: the
installed `TextTwist2.exe` (SHA-256 `0c6e962c…a9e801`), decompiled with Ghidra 12.1.4 through REA on
2026-10-15. Labels: **CONFIRMED** = read in the decompiled code or the shipped data, **INFERRED** = a
reading of the code that was not traced to the end, **UNKNOWN** = not traced.

Some facts were read with `objdump` or a Python string lookup on the same SHA-256 file, where Ghidra
defines no function; those rows say "objdump" instead of an evidence ID. The DRM DLL was not opened.

The port's own display work (resolutions, widescreen, refresh rate) is planned in
[`docs/design/improve-ui-ux.md`](../design/improve-ui-ux.md); every place it differs from the facts
below is listed in [Port deviations](#port-deviations).

## 1. Resolution: 800x600 only (CONFIRMED)

| Fact | Where | Evidence |
| --- | --- | --- |
| `application.xml` sets `gamewindow` and `renderer` to `width="800" height="600"`; the command dispatcher reads them | `0x46df00` | `ev_35174d01bf295b358220c4235ecf1e83be9f4ca11336453959b73742c3d6f6a4`, `ev_7858725f3cdc94f8f6d49364382d628b6f048291464c84ea0fff8e88e68261fc` |
| The back buffer's width and height come from renderer `+0x2c` / `+0x30` | present-params builder `0x4930c0` | `ev_1860798a1b0ed89ae400f9b5e133fb0d94527ff4e0ca459c10bb182a57eef8fe` |
| Windowed style `0xCA0000` (caption, system menu, minimize box), plus `WS_MAXIMIZEBOX` when fullscreen is supported; no `WS_THICKFRAME`, so the window cannot be resized. Fullscreen style is `WS_POPUP` with `WS_EX_TOPMOST` | `0x483770` | `ev_3d1e9618769ce72c278c0113810346c7d602380d840b610d6a0d697ff4b6dac2` |
| `AdjustWindowRectEx` sizes the window so the client area is exactly 800x600 | `0x483ad0` | `ev_4b5829e432b29f12e43e3658df5e6e858be6e8f522a0d907fbac12b0a08584c9` |
| The device check refuses a GPU whose maximum texture size is below 1024x1024 | `0x492d10`, `0x482810` | `ev_1fba6f125da09421e4b9354d76a8099d05d063face7b4e7a79f9feca7858a8ed` |

No letterbox or scaling code was found (INFERRED: the whole binary was not scanned for it). In fullscreen
the monitor does the scaling, because the game switches the display to 800x600 (section 3).

## 2. Renderer: Direct3D 9 only (CONFIRMED)

- `RendererD3D::init` (`0x492d10`) calls `LoadLibraryA("d3d9.dll")`, `Direct3DCreate9`, `GetDeviceCaps`,
  then `CreateDevice` with a HAL device and hardware or software vertex processing
  (`ev_2ef08d51235b50c43d034a2d8de01404060bdcaea5ee72df0ae1425d600a1fd3`). The library is loaded at run time,
  so `d3d9.dll` is not a static import ([`binary-fingerprint.md`](binary-fingerprint.md) §4).
- The hardware check `0x482400` shows `CTEXT_ERROR_NO_DRIVER`, `CTEXT_ERROR_WRONG_DIRECTX` or
  `CTEXT_ERROR_NO_HARDWARESUPPORT` and stops when anything fails
  (`ev_3b0a6d631b6ec95463e8542198409475b51f3edfb85482e8955ea9978f94bad4`).
- There is no OpenGL path (no `opengl32`) and no software renderer.
- The only `ddraw.dll` / `DirectDrawCreateEx` use is `0x508f70`, the audio library's DirectX version probe
  (`ev_dc79ce4373edcfc85e8814d25d42e444a3064863092d02860b5d16e9dba22767`,
  `ev_c7e3866955017026c78521c5a03f8730c7567f1b2c047d4077f2bbd0cfbff09f`).

## 3. Fullscreen: an exclusive mode switch (CONFIRMED)

- **Present parameters** (`0x4930c0`): `Windowed = !fullscreen (window +0x3c) || minimized (window +0x3f)`,
  the back-buffer format is the desktop's (objdump), `SwapEffect` DISCARD with one back buffer, and
  `FullScreen_RefreshRateInHz = 0`, which lets the adapter keep its current rate.
- **When fullscreen is offered** (`0x46df00`, gamewindow branch): `0x440d90` must find an exact 800x600 mode
  at the desktop's bit depth **and refresh rate**, and the desktop must be strictly larger than 800x600
  (`GetSystemMetrics`). The result is the window's "fullscreen supported" byte `+0x3d`
  (`ev_46dc0d8716026850a972e0c1357465a69795d2898fd47c465ab1f24b8ecc8af2`).
- **Switching** (setter `0x483c30`, `GameWindowWin32` vtable `0x597bd4` slot `+0x34`): the window is
  destroyed (`0x4836e0`), created again (`0x4837d0`) and shown, then a bool message is broadcast
  (`ev_18df6e88919196d824d0fc9f0277940c231d7368831a01d48dfc00ab0ad055fa`,
  `ev_c56a0b8e8ea73d54edb13bf83b9cdd2d63f3078c58eacb62c62690dc17d54c0e`). Who calls the setter (Settings OK,
  Esc, maximize; no Alt+Enter) is in [`help-and-settings.md`](help-and-settings.md) §3.4.
- **Device lost** (`0x493170`, objdump): `TestCooperativeLevel`; on `D3DERR_DEVICELOST` it waits, otherwise it
  rebuilds the present parameters and calls `Reset`.
- **Focus loss** (window procedure `0x483ef0`, objdump): `WM_ACTIVATE` clears the active flag `+0x3e`; in
  fullscreen it also minimizes the window (`0x483d20`, `ShowWindow(SW_MINIMIZE)`, sets `+0x3f`,
  `ev_e580386cefa99553d7012d4b5e596f2945c110e4249d7d8d3b2c9cdab588f7ac`). Activation restores it.
  `WM_SIZE` with `SIZE_MINIMIZED` sets `+0x3f`, `SIZE_RESTORED` clears it, and `SIZE_MAXIMIZED` switches to
  fullscreen. `WM_DISPLAYCHANGE` goes to `0x4838b0`, which was not traced (UNKNOWN).

## 4. Frame timing: a 60 fps cap, vsync and a 100 ms clamp (CONFIRMED)

- **Main loop** `0x46de50`: `PeekMessageA` until the queue is empty, then one frame, timed with
  `timeGetTime` inside `timeBeginPeriod(1)` / `timeEndPeriod(1)`. While the window is minimized (`+0x3f`)
  it calls `Sleep(40)` instead of running a frame
  (`ev_25f531ac9731ab3914495aa847e23d04696255acedcd2660e3260b96b0a6a4c0`).
- **Frame cap** (`TaskmanagerTimed`): the constructor `0x492460` sets the period `+0x40` to 16 ms
  (`ev_fb218e785895cf452ceed740b93067a3f5c4d3c614d49f3fc12b27e68c8802b6`). The `taskmanager` command reads
  `max_fps` (default `0x3c` = 60) and sets the period to `1000 / 60 = 16` ms by integer division (objdump,
  `0x46e56a`–`0x46e5ec`), so the real cap is about 62.5 fps. `application.xml` does not set `max_fps`.
- **Tick** (`0x4924c0`, vtable slot `+0x28`,
  `ev_818e5e3820970573f90e3f8f1c538e9a83bf95599086ffd01a53977043753deb`):
  1. `dt = now - previous`;
  1. if `dt < 16`, `Sleep(16 - dt)` and read the clock again;
  1. track the smallest and largest `dt`;
  1. **clamp `dt` to 100 ms**;
  1. run every task with that `dt` (`0x4903f0`).
- **Vsync**: `PresentationInterval` is `ONE` when renderer byte `+0x12` is set, else `IMMEDIATE`. The base
  `Renderer` constructor `0x480e30` sets it to 1
  (`ev_902f1f0e69739f0cf537ab338be2e73a9a5d4c81720bdf4db5fcfc1ae777763c`); it is the only write found in
  `0x480000`–`0x494000` (objdump), so vsync is always on (INFERRED: the whole binary was not scanned).
- **No 60 Hz anywhere else.** The only 60 is the `max_fps` default; every game timer is a millisecond delta,
  so the rules do not depend on the refresh rate. What `frame_by_frame="true"` (vtable `+0x44`) does is
  UNKNOWN.

### How the port sizes the window (UX-5)

`ui/include/tt2ui/display.hpp` holds the rules and `ui/src/display.cpp` applies them:

- **Sizes:** 800x600, 1024x768, 1280x960, 1440x1080, 1280x720, 1600x900 and 1920x1080. Windowed offers those
  that fit the display's usable bounds minus the window frame; a saved size that no longer fits steps down to
  the largest one that does. Native offers those with an exact display mode.
- **Borderless** is SDL3's fullscreen with no display mode: the desktop size, whatever the resolution says.
- **Native** switches the display mode like the original (section 3), keeping the desktop's refresh rate when
  the size has a mode at that rate. When the switch fails the window goes Borderless, the log says why and
  IMPROVEMENTS shows "Native unavailable, using Borderless". Wayland compositors (WSLg, gamescope) only emulate
  the switch: the output stays desktop-sized and the compositor scales.
- **The canvas stays 800x600.** SDL's letterbox presentation scales it into the output; `fit_4x3` repeats
  SDL 3.4's arithmetic (width floored, the rectangle centred, half pixels kept), and the log compares the two
  at start and on every pixel-size change. At 1920x1080 the game is 1440x1080 at x = 240; on a Steam Deck
  (1280x800) it is 1066x800 at x = 107.
- **High pixel density:** the window asks for real pixels (`SDL_WINDOW_HIGH_PIXEL_DENSITY`), so a scaled
  desktop gets a sharp canvas; sizes are in window points, output sizes in pixels.

### How the port keeps time (UX-7)

`ui/include/tt2ui/frame_clock.hpp` replaces the 16 ms `Sleep` cap:

- **`FrameClock`** reads `SDL_GetTicksNS()`. Each frame adds the elapsed nanoseconds (times `--speed`) to a
  carry and hands out whole milliseconds, because every model steps in `int` ms. The fraction stays in the
  carry, so at 240 Hz (4.1667 ms a frame) the steps run 4, 4, 4, 4, 4, 5 and add up to the real time.
  Flooring each frame's own duration would lose 0.1667 ms a frame, 4% of the game clock. (The 0.2.x loop
  subtracted whole-ms timestamps, which also adds up; the class keeps that property under unit tests.)
- **The 100 ms clamp stays**, and the time it cuts is dropped, as in `0x4924c0`. `--fixed-dt` bypasses the
  clock as before (the step is still clamped to 100 ms).
- **`FramePacer`** sleeps with `SDL_DelayPrecise` until the next frame start when a frame limit is set.
  Frames aim at fixed slots, so the average rate holds; a frame more than a slot late starts a new schedule.
- **Vsync** stays on by default, like `PresentationInterval` `ONE`. A renderer that refuses it disables the
  VSYNC row in IMPROVEMENTS.
- **Audit:** every model steps by the `dt` it is given. One behaviour is per frame: a `ModifierQueue`
  (`modifier.hpp`) that finishes an item exactly on a frame boundary starts the next one on the next frame,
  like the original. A boundary costs one frame, 16 ms at 62.5 fps and about 4 ms at 240 Hz, so chained
  animations such as the splash cards finish a few frames sooner on fast displays. That is accepted.

## Port deviations

Planned in [`docs/design/improve-ui-ux.md`](../design/improve-ui-ux.md). Each row is filled in by the phase
that ships it; until then the port matches the facts above as closely as SDL allows (800x600 canvas,
borderless fullscreen, vsync, `dt` clamped to 100 ms).

| ID | The port | The original (section) | Shipped in |
| --- | --- | --- | --- |
| UX-1 | Renders with Vulkan, then OpenGL, then software, through SDL3; `renderer=` in `improvements.txt` or `--renderer` picks the first to try; never Direct3D | Direct3D 9 only (2) | 0.3.0 (P1 SDL3, P3 order) |
| UX-5 | Window sizes from 800x600 to 1920x1080 (4:3 and 16:9) that fit the display; Windowed, Borderless (desktop size) or Native (an exact display mode at the desktop's refresh rate, else the fastest; Borderless when the switch fails); `window=`, `resolution=` and `fullscreen_mode=` in `improvements.txt` | 800x600 only; exclusive 800x600 mode (1, 3) | 0.3.0 (P5) |
| UX-6 | Side bars beside the 4:3 game filled in the original palette | No widescreen (1) | |
| UX-7 | No 60 fps cap by default; vsync (on by default) and a frame limit (60, 120, 144, 165 or 240) in `improvements.txt`; a nanosecond clock whose whole-ms steps keep the leftover fraction; the 100 ms clamp is kept | 60 fps cap with `Sleep`; vsync always on (4) | 0.3.0 (P4) |
| UX-8 | Smooth or pixel-art filtering, 2x placeholder art, full-resolution dialog layers | No scaling (1) | |
| UX-9 | Optional texture upscaler | None | |
