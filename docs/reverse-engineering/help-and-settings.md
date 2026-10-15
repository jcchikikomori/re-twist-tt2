# How to Play, Settings and Credits dialogs

Evidence for the pause-menu dialogs HOW TO PLAY and SETTINGS. Target: the installed
`TextTwist2.exe` (SHA-256 `0c6e962c…`), decompiled with Ghidra 12.1.4 through REA batch B on
2026-10-14. Labels: **CONFIRMED** = read in the decompiled code or the shipped data,
**UNCONFIRMED** = not traced, not built until it is.

This file records layout, timing and behavior only. The help **text and pictures are not
copied here**: the original instructions live in the four help images, which the game
loads at runtime once the player has imported their own install (`docs/design/import-assets.md`).

## 1. Who opens them (CONFIRMED)

| Opener | Opens | Evidence |
| --- | --- | --- |
| Pause menu HOW TO PLAY (`0x408180`) | `DIALOG_HELP` | `ev_d751091ad2fe2c0a05587976c1a0c6592fb6b930ad70d319c5b08305f77b61b6` |
| Pause menu SETTINGS (`0x408250`) | `DIALOG_SETTINGS` | `ev_cd8a7b2ab31b2eccb64ad0ca43bf775c4797da0b9091d436ea7b304ff43551f8` |
| Main menu (`0x42ee90` / `0x42ef5f`) | `DIALOG_HELP` / `DIALOG_SETTINGS` | batch B data pass |
| `DIALOG_TUTORIAL` `BUTTON_HELP` ("read more", `0x40e1f0`, wired at `0x40df2a`) | `DIALOG_HELP` | `ev_3dc938e4e7913913eb1167f2902a5817e450eb2f9a24af7504c33700bc61a16f`, `ev_facbcbbbf8dd61420bd0d5dc8932d4601e02d5dc961611bac91c4b9df777b14e` |
| Settings `BUTTON_INFO` (`0x40c760`) | `DIALOG_CREDITS` | `ev_b1c14da732041ff70b941eb684acf2069baee991e7cf80f5202c211b7056200e`, `ev_25dbac0ef9aae3efedbb5c5ea5ce2c43480f5ae285e235a3113b2765a7fee3bd` |

## 2. `DIALOG_HELP` (798×595, ctor `0x404830`)

Components (`properties/dialogs.xml`, CONFIRMED):

| Component | Position | Notes |
| --- | --- | --- |
| `LABEL_HEADER` | (394, 28) | "HOW TO PLAY" |
| `LABEL_INDEX` | (430, 30), anchor topright | No text in the XML, and no DialogHelp routine writes it (see below) |
| `HELP_PAGE01` .. `HELP_PAGE04` | (36, 83) | `IMAGE_HELP_PAGE0N`, 727×468, `images/gui/dialoghelp/`; page 1 starts at alpha 255, the others at 0 |
| `BUTTON_PREV` | (28, 507) | |
| `BUTTON_OK` | (400, 505), anchor top | closes (`0x408c00`) |
| `BUTTON_NEXT` | (776, 511), anchor topright | |

Behavior (CONFIRMED, `ev_50b6e5a9844d662570f9354ce6f20ba70b816c730bb1deba306cd94b193e3068`,
`ev_ce0dd14469919e76cca1dc06dfe09f29dfd153ad46bee0e689418c11c8537ddc`,
`ev_a6bc9405a11dc366d13efd4a7a0e30decedeb613b4d72e427fd8e15d2b74c143`,
`ev_03c750914279fb23d16044a0764a4f9c2fabe501e6adb21274e9f0eab741c6f7`):

- The page count is the number of entries in the `HELP_HEADERS` vector of `text.xml` (4).
- PREV (`0x404f10`, step −1) and NEXT (`0x404f30`, step +1) both call `0x404f50`, which
  fades the current page from alpha 1 to 0 and the new page from 0 to 1, each over
  **200 ms**, with easing `0x4307e0` that returns its input (**linear**).
- The page is clamped, never wrapped. PREV is visible iff `page > 0`; NEXT is visible iff
  `page < count − 1`.
- The `panels` text labels (`TEXT_HELP_1..4`) all hold the placeholder "Instructions not in
  place yet." The real instructions are drawn inside the page images.
- `LABEL_INDEX`: the ctor only caches the pointer at `+0x1e8`
  (`ev_b863573482e6b858306925d8afcb436a91ffad9d7ecf9e5b47cf7518244ef372`); the page
  change `0x404f50` does not touch it. Bounded negative: the label stays empty.

Keys shown on help page 1 (game controls, CONFIRMED by viewing the image, not copied):
Backspace removes the last letter, Delete clears all letters, Enter enters the word (or
repeats the last word when the row is empty), Space twists. Timed and Untimed allow two
passes per game (see `game-scene.md` §5).

## 3. `DIALOG_SETTINGS` (436×440, ctor `0x40bdf0`)

Components (`properties/dialogs.xml`, CONFIRMED):

| Component | Position | Notes |
| --- | --- | --- |
| `CTEXT_SETTINGS_HEADER` | (226, 33) | "SETTINGS" |
| MUSIC label / `SLIDER_MUSICVOL` | (45, 94) / (123, 98) | `maxvalue 1`, knob x 30..276, y 12; `slider_back` 284×19, `slider_button` 33×140 (4 rows) |
| SOUND label / `SLIDER_FXVOL` | (43, 145) / (123, 149) | same slider |
| `SWITCH_FULLSCREEN` / `LABEL_FULLSCREEN` | (161, 208) / (189, 207) | disabled when `[0x5fdea0]+0x3d == 0` |
| "MAXIMUM WORD LENGTH" / `DIFFICULTY_TAB` | (107, 263) / (220, 299) | `difficulty_tab` 383×102 |
| `SWITCH_LENGTH_6` / `_7` / `_8` | (46, 298) / (170, 298) / (289, 298) | radio group 30, `button_check` 24×200 (8 rows); labels "6 Letter" (79, 309), "7 Letter" (202, 309), "8 Letter" (320, 309) |
| `BUTTON_INFO` / `BUTTON_OK` / `BUTTON_CANCEL` | (85, 357) / (278, 357) topright / (294, 357) | 119×200 images |

Behavior (CONFIRMED):

| Fact | Evidence |
| --- | --- |
| Ctor wires FX slider `0x40c5c0`, OK `0x40c620`, INFO `0x40c760`, CANCEL `0x40c700`, music slider `0x40c5f0` | `ev_3f3c1bd0996051ca59751fbab77ba6ada0b48a519c8a54cf9d602c40da43ceaa`, `ev_eae3101792652643e0b6361e4d74656a930ec9edd6033ea21b99568ec52e14f5` |
| The checked length switch is the **current player's** max length (`[[0x5fde98]+0x70]+0x3c`): 7 → `_7`, 8 → `_8`, anything else → `_6` | same |
| Sliders apply live: FX → `0x463db0(fx)`, music → `0x463e30(stream, music)` | `ev_863b712504be03fa051aa066abf275dfaa9287eb3f4f7bc2ac95f048632883d4` |
| While the FX knob is held (flag `2`), the update hook (vtable `0x58f304` slot 25, `0x40c3b0`) plays `SOUND_SLIDER_1` at most every **500 ms** (`timeGetTime`) | `ev_eb46bc37eb333daf7e6e450e016ef0c7e62e4d7a2eb6a458bef3c3ff23b34dc4`, `ev_21351c803c9d3f686a7b359c91a4b56bd67d6b0b7aa6a21eb4a0bec5cd0d457b` |
| CANCEL re-applies the **saved** FX and music volumes, then closes | `ev_5f5de7fa2647b8f5674660a45f4621c0f1b4967395e07265dd5d0d29ecfc0e7a` |
| OK saves the settings (`0x4668c0`), applies fullscreen, stores 8 / 7 / 6 into the player's max length from whichever switch is checked, saves the players (`0x462080`), then runs CANCEL's apply-and-close | same |
| OK makes no call into the running scene, so a new max word length first applies at the next rack pick (round start); this answers plan item F13 | same |
| Show is the shared `0x40aa30` (slot 44): no stream call, same as the pause menu | `ev_21351c803c9d3f686a7b359c91a4b56bd67d6b0b7aa6a21eb4a0bec5cd0d457b` |

Storage (CONFIRMED, `ev_aa42bd9904bed645926e3ecbc6676edb9e8451234a79dcd0afa8889fb7bbcab4`):
`SettingsManager` (`0x466300`) loads the group `"GameSettngs"` (sic) with `0x4666b0`, which
enumerates `HKEY_CURRENT_USER\<app key>` (`0x466f70`, REG_SZ and REG_DWORD values). Volumes
and fullscreen are global; the max word length lives in the player profile. Our port keeps
volumes and fullscreen in a plain-text settings file instead of the registry (plan task 6.3).

### 3.1 Values, defaults and ranges (REA batch C, 2026-10-14)

`TextTwist2GameSettings` (vtable `0x58f4f4`, base `0x58f2bc`) is created by the factory `0x40e720`.
Its getters and setters (vtable slots `+0x14`..`+0x30`) pair one field with one registry value:

| Field | Registry value | Type | Default (`0x40e720`) | Range |
| --- | --- | --- | --- | --- |
| `+0x04` | `fullscreen` | bool | 1 | 0 / 1 |
| `+0x05` | `usehints` | bool | 1 | 0 / 1 (no control in `DIALOG_SETTINGS`) |
| `+0x08` | `soundvolume` | float | 0.5 | clamped to 0..1 by `0x463db0` |
| `+0x0c` | `musicvolume` | float | 0.5 | clamped to 0..1 by `0x463e30` |
| `+0x10` | `onlinescores` | bool | 1 | no control |
| `+0x11` | `showmoregames` | bool | 1 | no control |
| `+0x18` | `moregamesurl` | string | empty | no control |

- Load `0x465a80` only overwrites a field whose registry value exists; save `0x465830` writes
  `fullscreen`, `usehints`, `soundvolume` and `musicvolume`.
- `DIALOG_SETTINGS` copies the live values into its own instance with `0x466290` and keeps them for CANCEL.
- **Port deviation:** our `settings.txt` stores `fullscreen`, `soundvolume` and `musicvolume` only, and a
  first run is **windowed** (the user's choice on 2026-10-14). The other values have no control in this
  dialog and are not stored.

Evidence: `ev_363422672f773da1de8157b056462c00b71435b19d8b9d3c72c71060ccdbc911` (factory, getters, setters),
`ev_117a15868e7cbfe1802077f03124cb59329a9c6b4789638bbb7f51f8c84afee2` (vtable),
`ev_1e8ed9080218046e1bb157e0279205d6df8dbfb306be2f3fcea553ae1489352c` (load and save),
`ev_faf2da26497fcd65838cd4a36b641d91969d7ae681597f5dcf9140f7c782c1ff` (copy),
`ev_70c757186b69de6ab97792324b83020f75061b4f995872ffd9a301489256aca4` (clamps).

### 3.2 Slider and switch geometry

- The slider parser `0x467890` reads `minvalue`/`maxvalue` and the knob track `minx`/`miny`/`maxx`/`maxy`.
  `0x468440` places the knob (a child button, anchor center) at
  `x = minx + t · (maxx − minx)` with `t = (value − minvalue) / (maxvalue − minvalue)`.
  Worked example for `SLIDER_MUSICVOL` at (123, 98), track 30..276, y 12: value 0.5 puts the knob centre
  at dialog-local `(123 + 30 + 0.5 · 246, 98 + 12) = (276, 110)`.
  Evidence: `ev_7246d3cff0b82457e234633e717d381ad11f5322522f7eb0722765a02038bc87`,
  `ev_0f56c3eaff2aaadbb4fa6f1cf857e1a0773eea42ddfe9a6de515e50aa1357690`.
- **INFERRED:** pressing on the track or the knob moves the knob to the pointer; the press handler was
  not traced.
- **INFERRED (from the image):** `CHECKBOX02` (`button_check.png`, 24×200, 8 rows of 25) holds the off
  states in rows 0–3 and the on states in rows 4–7, each as normal, hover, pressed, disabled.

### 3.3 Per-sound volume attribute

`resources.xml` writes `volue="0.5"` on most sounds and streams, but the parsers `0x471ed0` (sound) and
`0x472590` (stream) look up `volume`, and the string `volue` does not occur in the executable. Those
sounds therefore play at the resource default; only the entries spelled `volume` (for example
`SOUND_CHECKBOX_ON`, `SOUND_SLIDER_1`) are set to 0.5. The resource default itself is **UNCONFIRMED**.
Evidence: `ev_6ff35b71b9d47934d1bf06afbd433db0f2ae0744310c4311589f3d5027cae6d4`,
`ev_2cec752d01ce146862067d7d2593a01aaf38787e06382d6b083a7a72d5a83fcb`,
`ev_a1ebda5e7e861433e01015e008026b69222125c68bf8c3122f8002077ad199be`.

### 3.4 Window mode shortcuts (REA batch D, 2026-10-14)

`GF2::GameWindowWin32` (ctor `0x4834c0`) registers the window procedure `0x483ef0`; byte `+0x3d` of the
window object is the "fullscreen supported" flag. The fullscreen setter is the window's vtable slot
`+0x34`, and only three paths reach it:

| Trigger | Effect | Where |
| --- | --- | --- |
| Settings OK | the FULLSCREEN switch value | `0x40c620` |
| Esc key down (`WM_KEYDOWN` or `WM_SYSKEYDOWN`) | `+0x34(0)`: windowed; the key is still delivered to the game | key translator `0x484770`, engine key `0x10` (table `0x5d3228`: `0x1b`) |
| Maximize (`WM_SIZE`, `SIZE_MAXIMIZED`) | `+0x34(1)`: fullscreen | `0x484036` |

- **There is no Alt+Enter.** The translator's only modifier check is Ctrl+P (`0x5d3114` = `VK_CONTROL`,
  message `0x601`). The window procedure handles no `WM_SYSKEYDOWN` / `WM_SYSCOMMAND` of its own. The
  renderer loads `d3d9.dll` / `ddraw.dll`, neither of which adds Alt+Enter. A scan of the whole
  executable found no other caller of the setter through `[0x5fdea0]`.
- **Port deviation:** Alt+Enter toggles the window mode and saves it to `settings.txt` (the user's choice
  on 2026-10-14). The original Esc and maximize shortcuts are not ported: Esc keeps opening the pause menu.

Evidence: `ev_3aba74211b0566c537bff5aa1bb483e505f8ed72805ff11b5181f2b0d452a913` (window ctor),
`ev_f68b85a9df92a2795e664a20cce08b6e0f3f81cdf40322e8ce67bac27dfcd071` (WndProc entry bytes; the body was read
with `objdump` on the same SHA-256 file, since Ghidra defines no function there),
`ev_aac3f1f9207e7e38b348c1884eca4252fdb4b7a66d8cc96e13cb2ca53989600a` (key translator),
`ev_59931fce2ea19ee97906b5e0729427b3e0ea3f2ac2bbbcc4f328666869e4b501` (key table),
`ev_dfc3c46687e02c004ceff56f48a91351a88a5b1777d750116935464454b09973` (`VK_CONTROL`),
`ev_7921652f82cfa97b132442994717c96b3aac08bb27c04d86fcd1c3bcafe35475` (Settings OK),
`ev_f84f3f119c006d479b543c7b7c3cd7cf47db99503d55f2c8eb29446277279d7e` (`GetKeyState` callers).

### 3.5 Port deviations: the asset row (OP-7, 2026-10-15)

The original `DIALOG_SETTINGS` has no asset row; the layout above stays the documented original. The port
needs the player's own Text Twist 2 install for its art, fonts and sounds until the placeholder art is ready
(`docs/design/import-assets.md`), so the **main menu's** SETTINGS gains one row. The in-game SETTINGS (from
the pause menu) keeps the original layout.

| Item | Original | Port, main menu only |
| --- | --- | --- |
| Dialog height | 440 | 490 (one 50 px row) |
| `BUTTON_INFO` / `BUTTON_OK` / `BUTTON_CANCEL` y | 357 | 407 |
| `LABEL_ASSETS` "ORIGINAL ASSETS" | none | (45, 357), `FONT_SETTINGSTEXT` |
| `LABEL_ASSETS_STATUS` | none | (45, 381): "Imported", "Not imported" or "Needs updating" |
| `BUTTON_IMPORT` | none | (391, 357), anchor top-right; disabled without a per-user folder or manifest |

- IMPORT keeps the dialog's values like OK, closes it, and opens the folder picker straight away (no launch
  warning). Cancelling the picker closes it.
- The original game has no art for IMPORT, so it is drawn as a placeholder button with its name, even when
  the original art is imported.

## 4. Dialog keys (bounded negative, REA batch B2)

Dialogs take **no keyboard shortcuts**. The input listener of `DialogSettings`
(sub-vtable `0x58f49c` at `+0x174`) implements only mouse press (`0x43fa70`), release
(`0x43faf0`), drag with an 18 px² threshold (`0x43fb50`) and a capture reset (`0x43fc70`).
Every other slot is `0x401d60` (`xor al, al; ret 4`, "not handled"). No install XML binds
a `KeyStroke`. Search boundary: the dialog listener and the install XML; a global hotkey
registered elsewhere in code is not ruled out.
Evidence: `ev_f3b85f73d8289c82bd02881343886ca8df4ef8217ccd532726c110299fe8cc84`,
`ev_9e5f44716e7bbf661c14e195b07ea64be43240ddcf791fd73fd902c858f74435`,
`ev_1eebccb343a62100bf5972f482020736ec4f3952313372be5c12709aa5708f65`.

## 5. `DIALOG_CREDITS` (798×595, ctor `0x403e90`, REA batch H)

Settings `BUTTON_INFO` (`0x40c760`) shows the dialog over `DIALOG_SETTINGS`, which stays
open under it. Components (`properties/dialogs.xml`, CONFIRMED):

| Component | Position | Notes |
| --- | --- | --- |
| `HEADER` | (397, 160) | `CTEXT_BETA_INFO_HEADER` "CREDITS", `visible="false"` |
| `LOGO_STATIC` | (338, 30), scale 0.5 | children `T` (`IMAGE_LOGO_COIL`, −160, −18) and `2` (`IMAGE_LOGO_BOUNCE`, 228, −34) |
| `VERSION` | (771, 87), anchor right | `FONT_FONT_HIGHSCORE_TEXT`; the ctor overwrites the XML text with the app version |
| `GRA_CREDITS` | (46, 124) | `IMAGE_GRA_CREDITS`, 708×394: a picture of the 2008 GameHouse staff list |
| `MESSAGE` | (32, 534), anchor left | `TEXT_CREDITS0`, width 370, word wrap |
| `BUTTON_OK` | (714, 535), anchor centre | `0x408c00`, the shared close |

Behaviour (CONFIRMED):

- The ctor `0x403e90` wires `BUTTON_OK` to `0x408c00`, parses `clipy` (`+0x1e0`) and
  `clipheight` (`+0x1e8`) with `sscanf("%f")`, reads `scrollduration` (`+0x1ec`), zeroes a
  timer (`+0x1f0`) and an empty `GFVector<Label*>` (`+0x1f4`), and sets `VERSION`.
- Show (vtable slot 44, `0x4041e0`) is the base show plus the painter render; no other slot
  is overridden.
- **The credits never scroll.** Bounded negative: in `0x403e00`-`0x404830` nothing reads
  `+0x1ec`, `+0x1f0` or the label vector after the ctor, and the strings `startdelay` /
  `enddelay` are absent from the exe. `TEXT_CREDITS1..8` are never shown.

Evidence: `ev_6e6a9b2f0860f144078fced097ce2b18e6765717bf945f04e2560058f5ec86aa`,
`ev_49d2920460ab1321d639fa671b3ab38f75cd91b4f94cce3527877e2163eaf3fc`,
`ev_77a3851f642296ef0540b12c30f289aa812ef5d2955db8d9fa99fca66a7e84c6`,
`ev_e9367480200d5ad56f1de990a54373532a77e94116052ffa0ea3a029efe8b340`,
`ev_38cb2daaaa0e59ebc0668b0e8be3c69c97f6822ee1c9e31170fcbf13083ca9eb`.

Port (`credits_dialog.hpp`, owned by `SettingsDialog`): same frame, logo, `VERSION` (the
CMake project version) and OK, over two pages.

- **Page 1 is the original credits.** `GRA_CREDITS` and `TEXT_CREDITS0` sit where the original
  puts them. The picture loads only from the user's `imported-assets/`; without it, the
  `P0_ORIG_*` labels draw the same roles and names as text in the picture's layout (each label's
  `fallback_for` names the sprite it stands in for).
- **Page 2 is this recreation.** Deliberate deviation: a second page, reached with NEXT (the help
  dialog's PREV/NEXT art at (580, 535)) and crossfaded with `HelpPager` over 200 ms, credits Claude
  (Anthropic), who generated the code, the person who compiled it, the open word lists and SDL2,
  and its `MESSAGE` says the port is not affiliated with GameHouse.

INFERRED: a child sprite's offset scales with its parent, and the logo parts show frame 0.

