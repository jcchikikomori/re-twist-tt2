# Tooltips

Evidence for the hover tooltips (`GF2::Tooltip`, `GF2::ToolTipLabel`). Target: the installed
`TextTwist2.exe` (SHA-256 `0c6e962c…`), decompiled with Ghidra 12.1.4 through REA on
2026-10-15. Labels: **CONFIRMED** = read in the decompiled code or the shipped data,
**INFERRED** = a reasonable reading the code doesn't settle, **UNKNOWN** = not traced.

The port's model is `ui/include/tt2ui/tooltip.hpp`; `ui/src/main.cpp` draws it.

## 1. Timing (CONFIRMED)

| Fact | Value | Evidence |
| --- | --- | --- |
| Tooltip ctor `0x474390`: show delay `+0x38` | 1000 ms | `ev_25937d937873b56e432d067e91ae885f118de1845e27f9a255da141195abbc2b` |
| Same ctor: time on screen `+0x40` | 10000 ms | same |
| Per-frame update `0x4748d0` | see below | `ev_d03c307e2a1de591d23d3d586c1f7b1e893977289305dffd840331209677de5b` |
| Hovered-widget walk `0x474ae0` | see below | `ev_546639c4ef35fd19ac976b28a868879013fb5a3734d98685c6875f19fa709c92` |

- `0x474ae0` walks the widget tree depth-first for the hovered widget that has a tooltip
  (`+0x150`). A widget of type `DAT_5fe410` (a dialog) clears the result, so a dialog hides
  the tooltips of whatever is under it.
- A different widget restarts both timers. On the same widget the delay counts down by the
  frame time; when it reaches ≤ 0 the box is placed once, at the cursor of that frame.
- The box shows while the delay is spent and the time on screen is not; the time on screen
  only counts down while it shows. After 10 s it stays hidden until the cursor reaches
  another widget.

## 2. Placement (CONFIRMED, `0x4748d0`)

1. x = mouse x, y = mouse y + 24.
1. The edge tests use the box size + 3.
1. No room below: x += 16, y -= 24, then clamp y to `screenH - h`.
1. No room on the right: x = mouse x − w − 2 when step 3 moved it, else `screenW − w`.

Worked example, QUIT on the main menu: cursor (720, 558), a two-line box `w` × 34. Below
fails (`558 + 24 + 37 > 600`), so x = 736 and y = 558 (`558 + 37 ≤ 600`, no clamp); the
right then fails (`736 + w + 3 > 800`), so the box sits left of the cursor at
x = `720 − (w + 3) − 2`.

## 3. Look (CONFIRMED)

| Fact | Value | Evidence |
| --- | --- | --- |
| ToolTipLabel ctor `0x473ac0` background | Color(1, 1, 1, 0.882) in A, R, G, B order = `#FFFFE1` | `ev_7240302cf93d1843fc568893cb34619a8ce3911ea35bdf98fd891bc3d9ad57b3` |
| Border (`0x5d2304`) | Color(1, 0, 0, 0) = black | same |
| Padding | 3 | same |
| Loader `0x473be0` reads | backgroundcolor, bordercolor, padding, maxwidth | `ev_c59c98de13691ba38ffb2c6a4c09e1fd6282dd948be66d79d1d5859c3773090e` |
| `dialogs.xml` `DEFAULT_TOOLTIP` | font `FONT_TOOLTIP` (`fonts/arial12.xml`, a mask layer tinted `#000000`), padding 3, alpha 220, maxwidth 200 | shipped data |

The placeholder font is Arimo Regular at the original `arial12` metrics ("A" height 11,
baseline 3, space 3, sample width 316), drawn by `tools/gen_placeholders.py`.

## 4. Which widgets have one (CONFIRMED, `dialogs.xml` + `text.xml` / `commontext.xml`)

| Surface | Widgets |
| --- | --- |
| Main menu | CHANGENAME, PLAY / PLAY_WOD, HOWTOPLAY, SETTINGS, HIGHSCORES, QUIT |
| Classic scene | Twist, Enter, LastWord, Clear, NextRound, Pass / Pass_2 / GiveUp, Menu |
| `DIALOG_MENU` | HOWTOPLAY, SETTINGS, STOP, RESUME |
| Round dialogs | OK, HELP, REPLAY |
| `DIALOG_PLAYERSELECT` | ADD, REMOVE, OK, the name rows |
| `DIALOG_SETTINGS` | INFO, both sliders, the fullscreen and max-length switches |

Quirks kept as shipped:

- Every round-dialog button uses `CTOOLTIP_NAMESELECT_OK` ("Play as the selected player").
- The max-length switches reuse the fullscreen text, which says "to to play in a window".
- **INFERRED:** QUIT names `CTOOLTIP_SELECTION_QUIT`, which no text file defines; the port
  uses `CTOOLTIP_SELECTION_QUIT_WIN`.
- **Not built:** the settings labels and the new-player name input also carry tooltips;
  the port has no hover state for them yet.
