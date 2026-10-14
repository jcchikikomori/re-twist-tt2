# How to Play, Settings and Credits dialogs

Evidence for the pause-menu dialogs HOW TO PLAY and SETTINGS. Target: the installed
`TextTwist2.exe` (SHA-256 `0c6e962c…`), decompiled with Ghidra 12.1.4 through REA batch B on
2026-10-14. Labels: **CONFIRMED** = read in the decompiled code or the shipped data,
**UNCONFIRMED** = not traced, not built until it is.

This file records layout, timing and behavior only. The help **text and pictures are not
copied here**: the original instructions live in the four help images, which the game
loads at runtime from `imported-assets/` when the user has run `scripts/import-original.sh`.

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
