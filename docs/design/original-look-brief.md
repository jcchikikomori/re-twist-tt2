# Text Twist 2 — Design Reference Brief

> Internal visual reference only — derived from analysis of the original 2008 art.
> Nothing here may be copied into the product; all visuals must be original CSS/SVG/canvas.
> Source: @observer analysis of `images/{splash,loadscene,selectionscene,gamescene}/` in the local TT2 install, 2026-10-11.

**Canvas**: all scenes 800×600. Assets are 2008-era sprite sheets — designer should treat hex values as direction, not literal tokens.

## 0. Global system (cross-scene)

- **Sky/radial backgrounds**: royal-blue → cyan radial. Outer `#0536C6`/`#125FD9` → mid `#1554DC`/`#1867E2` → core `#15C7E9`/`#17CDED`. Light source = center of screen.
- **Concentric ripple rings**: subtle alternating bands radiating from center, ±6% luminance steps of the blue above (visible in selection + game bg).
- **Accent gold/yellow**: pinstripe `#FFEA00`, button border gradient `#F3A804 → #FDDF0C`, hover fill `#F7DE2B`, popup border `#D7C048`.
- **Deep panel blue**: `#0033BA` (panel fill, slot tiles), recessed dark `#001867`/`#011C6A`.
- **Special lime**: game-over border `#CEFF01`, commentary titles `#AEEA21`.
- **Error red**: `#FA0004` (error underline bar, timer tile `#FF050D`).
- **Typography system** (3 voices):
  - UI pills: bold condensed sans, uppercase, white fill + navy `#161E64` outline (or blue `#183A9F` on gold hover) — Arial-Bold-Condensed feel.
  - Mode/logo comic: heavy rounded comic display, yellow `#F4DA31→#F9E659` fill w/ white top-gloss, thick blue `#1048A6` stroke, hard drop shadow, cyan glow halo — "Luckiest Guy" vibe.
  - Ball letters: bold sans (Arial-Bold-like), uppercase, blue `#183A9F`, ~55–60% of ball diameter cap-height, subtle dark bottom shadow.
- **Ball system**: 60×60 px sprites, 5 columns × 26 letters (A–Z rows) in `tt_balls.png`/`letterball_anim.png` (+ `_yellow` variants). Frame states: (1) blank white ball, (2) blank w/ shading, (3) lettered, (4) lettered w/ pressed shadow, (5) lettered on blue rounded-square backing = "used/found" state.

## 1. Splash (`splash/background.jpg`)

- Pure black `#000000` field, publisher branding only, centered: GameHouse isometric house-cube (white left face w/ red `#E8443A` "G", yellow-orange `#F8C060` right face w/ cyan `#29A0D8` "H"), cyan drop-shadow offset; wordmark below = white letters on tilted cyan-outlined tiles + ® mark.
- No game art, no motion. (Homage: skip publisher branding entirely; use your own mark or none.)

## 2. Loading scene (`loadscene/`)

- **bg.png**: plain radial `#1554DC` → `#15C7E9`, nothing else.
- **ANIMATED_CIRCLES.png**: 10-frame 600×600 sheet — pulsing concentric rings overlay (expanding ripple loop).
- **logo**: "TEXT TWIST" stacked 2-line wordmark, chunky yellow letters; the T of TEXT is oversized, its stem a **screwdriver/tool shaft wrapped by a gold coil spring**; the "2" is a **white glossy ball with blue numeral** perched top-right. Animation: ball drops onto the T, coil compresses/bounces. `logo_anim` = keyframed sheet, `logo_bounce` (1728×252, ~8 frames of 216×252) = ball bounce loop, `logo_coil` (3298×253, ~8 frames of 412×253) = spring coil compress loop. Wordmark letters: yellow `#F4DA31` w/ white gloss top + blue `#2050C8` outline.
- **spring.png**: 800×600 overlay — big glowing blue spring/coil silhouette (`#125CCF` body, `#1DDAF1` glow) sweeping the width inside a **thin 1px white-bordered rectangle** (the logo's spring stage).
- **Loading bar**: 483×69 — a row of **8 blue rounded-square slots** (fill `#0330AC`, ~6px radius) each holding a recessed dark navy ball; as loading progresses each slot is replaced by a white glossy letter ball spelling **L-O-A-D-I-N-G**; gold `#F7C05C` baseline strip under the row. Empty vs full are two separate PNGs.
- **Loading… / Click here to start**: `loading_wait` / `click_here` — white `#FEFEFE` bold condensed text with navy `#192435` hard shadow; click-here sits centered under the bar once ready.
- **GH_Branding.png** (313×70): small house-cube + tile wordmark bottom-left; copyright line "Copyright © 2008 GameHouse, Inc." in small white sans.
- **Flow**: splash (GH, ≥2.1 s) → 500 ms fade → loading (bg + ripple pulse + logo drop + bar fill, held ≥3.5 s) → 500 ms fade → menu. The "Click here to start" sprite exists but is never shown in the shipped build; the scene advances by itself. Timings and addresses: `docs/reverse-engineering/welcome-flow.md`.

## 3. Selection / menu (`selectionscene/`)

- **bg.png**: ripple-ring blue field + the glowing spring motif stretched horizontally across center inside the thin white-bordered rect (spring = the game's core metaphor, persists behind the menu).
- **background_circle.png** (291×291): soft white ring (~25px thick, feathered edges, ~40% opacity) — radial-menu hub; mode labels orbit it.
- **Logo**: `logo_static` = final wordmark rendered in the game's jumbled tile-letter style — yellow/white letters overlapping at slight rotations, blue extrude `#1048A6`, drop shadow; sits top-center.
- **Mode buttons** (label strips with iconography; tall multi-frame sheets = idle→glow hover cycles, ~5 frames):
  - TIMED — silver clock face, hands + orange `#F3A804` circular arrows, cyan glow halo.
  - UNTIMED — cluster of plain white glossy balls.
  - LIGHTNING — orange bolt across a white ball, blue outline.
  - CROSSWORD — mini crossword grid of white cells + blue filled cells.
  - WORD OF THE DAY — spiral notepad w/ blue-lined page.
  - All: comic type, yellow fill + white gloss + blue stroke + drop shadow + outer cyan glow; arranged as a ring around center PLAY.
- **PLAY** (center, 119×200 = 4 states): pill 119×50, normal = deep blue gloss `#161E64→#193CA3` with thick gold border `#F3A804→#FDDF0C`; hover = full gold fill `#F7DE2B` w/ blue label; pressed = pale cream `#F0DF58`; disabled = gray `#BCBDB0` desaturated.
- **High Scores / Trophies / Settings / How to Play / Quit** (157×208 = 4 states of 157×52): same pill, smaller; label white w/ navy outline.
- **change_name.png** (155×52, 4 states): bare text link "CLICK TO CHANGE NAME" — white → cyan hover → gray inactive, bold condensed w/ navy shadow. No chrome.

## 4. Game scene (`gamescene/`)

### Background
- **background.png**: ripple rings + **floating glossy periwinkle balls** (`#91C2F7` body, `#65A5F7` shade, `#75C5F9` specular streak, cyan glow rim) at varied sizes 60–220px, scattered around edges leaving center clear.
- **background_timed.png**: same field but a huge **translucent clock** (light-blue `#8ECAF7` dial w/ tick marks + hands, cyan glow) centered behind the rack.
- **background_wotd.png**: variant for word-of-day (not analyzed in depth).

### HUD / panels
- **ui_bar.png** (800×54): top HUD strip — dark→mid blue gradient `#0F3394`/`#235ED4`, bright **gold 2px pinstripe `#FFEA00` along top edge**, soft highlight curve across upper third, black drop shadow bottom edge. Score/round/timer sit here in white/yellow bold sans.
- **panel_bg.png** (800×195): bottom panel — **semi-transparent `#0033BA` @ ~50% alpha**, flat (no texture); assembled with 9-slice pieces `panel_sideL/R` (10×1), `panel_cornerL/R` (10×10), `panel_bottom` (1×10) — i.e. a rounded/solid slab drawn via 9-slice, letting the blue bg show through.
- Layout rhythm (800×600): HUD bar top 0–54px; rack + entry zone center ~54–405px; blue word panel bottom ~405–600px; control pills stack bottom-right column (~2 cols × 119px + wider 157px buttons at very bottom edge).

### Found-word slots (`letterBox.png` 69×138 = 2 frames of 69×69)
- Rounded square (~8px radius), fill `#0033BA`, 1px darker navy rim, **dark navy `#001867` recessed circle** inset in the center — slot "hole".
- Filled state: **dark glossy blue ball** (`#355CBD` body → `#011C6A` shade, white top-left specular) with white/blue letter sits in the recess — i.e. found words read blue-on-blue, low contrast, calm.
- `letterBox_shadow.png` (69×7): thin soft dark line shadow under each tile.
- `yellowBox.png` (69×69): the **current/active slot cursor** — bright chartreuse-yellow frame (`#FFFF02`→`#B9C801` glow border) around the dark recess.
- `letterBox_cword/cwordhole`, `XwordBox`, `crosswordHighlight` (magenta `#B300FF` = engine highlight mask): crossword-mode cell variants.
- `red_tile.png` (14×17, `#FF050D`) + `errorBox.png` (105×21, `#FA0004` on black): wrong-word red underline flash.

### Letter rack balls (60px)
- Perfect circle. Body: near-white **`#ECECEC`** center, `#D6D6D6` rim falloff, heavy `#717372` bottom-left contact shade, bright top-left sheen — classic 2008 "pearl" ball.
- Letter: bold sans uppercase, blue **`#183A9F`**, slight inner emboss, no outline.
- Yellow variant sheet (`letterball_anim_yellow*`) for special modes/active.
- `red_ball.png` is actually a **cyan/teal glossy ball** (`#6DD3E4`/`#9AE4EF`) with a small silver figure icon — hint ball; `gray_ball.png` (55×55, `#282828→#6F6F6F` matte) = disabled/blitz ball; `particleball.png` (18×18 gold) + `blitzdot.png` (16×16 white) = particles.

### Entry row / anagram tiles
- `anagramTile.png` (20×40): small white/yellow (`#FBEA57`) tile w/ dark outline — used in mini anagram grids.
- `tt_balls_text.png` / `hint_letters.png` (621×207): A–Z alphabets — solid blue `#1048A6` (reveal) / light-blue outlined w/ shadow (hint), bold sans, 3 rows.
- `anagram_letters*.png`: white cells, 1px black border, black letters; highlight = cyan cell (valid) / yellow cells + red slashes (used/invalid) — crossword overlay style.

### Control buttons
- **Small pills 119×50** (4 states each): TWIST, ENTER, CLEAR, LAST WORD, PAUSE, MENU, ZOOM(68×296 strip), RESUME/STOP.
- **Wide pills 157×52** (4 states): DONE, NEXT ROUND, GIVE UP, PASS.
- Shape: full-radius stadium, **thick gold gradient border** (`#F3A804` bottom → `#FDDF0C` top), deep blue gloss body `#161E64→#193CA3` with vertical light streak, soft black drop shadow below.
- States: normal (blue/gold) → hover (gold fill, blue `#183A9F` label, white gloss cap) → pressed (pale cream `#F0DF58`, label white/cyan) → disabled (gray `#BCBDB0`, flat).
- Labels only — **no iconography on gameplay pills** (no twist arrows; the word carries it). Center-aligned bold condensed uppercase, white with 1px navy outline + 1px inner gloss.
- Menu/zoom/pass variants reuse identical chrome, only label differs (this is the system to replicate: one pill component, N labels).

## 5. Celebration / feedback elements

- **popup_bingo.png** (1900×420 = 5-frame shimmer sweep, frame 380×420): banner — rounded rect, cyan→blue gradient fill (`#3EADCF`→`#1C68DF` w/ swirl watermark), **double border: 3px gold `#D7C048` outside, 1px white inside**; copy: "You found the bingo word!" white bold rounded w/ blue outline, "NEXT ROUND!" large yellow w/ blue outline + hard shadow; a white gloss highlight bar sweeps L→R across frames.
- **popup_allwords.png** (2280×420 = 6 frames): same banner, "CONGRATULATIONS!" (yellow, outline) / "You found all the words!" (white).
- **tt_commentary.png** (189×648 stack): small stacked message banners — blue `#0271D2`/`#138AD7` fill, yellow-lime `#AEEA21` bold uppercase title, white body text, thin border; used for round status chatter.
- **star.png** (50×50): soft 5-point star, cream→gold gradient `#FDF9D3`→`#FCE899`→peach `#F7B071` edge, no stroke — score/combo sparkle.
- **tt_gameover.PNG** (234×116): two stacked banners — "GAME OVER" bold condensed **cyan-white `#DEF0F4` on blue-cyan gradient `#22B4C6`→`#076A84`, lime `#CEFF01` 3px border**; second banner (cyan fill, lime border) "WORD OF THE DAY FOUND!".
- **image_popup_0…12**: error/toast dialogs ("That word isn't in the the dictionary.") — blue radial-gradient rounded rect (`#1CA9E8`→`#1560D8`), gold 2px border + white inner hairline, white bold rounded text w/ soft shadow.
- **achievement.png** (750×700, 3×7 grid): toast tiles — translucent dark-gray rounded card, thin white border, trophy glyph, "New Achievement!" white bold + 1-line description; slides in at screen edge.

## 6. Modernization suggestions (2026 rebuild)

**Preserve (the nostalgia contract)**
- The **blue radial sky + ripple rings + floating glossy balls** — it's the identity. Rebuild as layered CSS `radial-gradient` + 4–5 concentric `repeating-radial-gradient` rings at 4% alpha steps, or a single SVG.
- **White pearl balls with blue letters** and **blue-on-blue found slots** — keep shapes, keep the calm low-contrast found state; this is what people remember.
- **Gold-bordered stadium pills** with the blue→gold→pale-yellow state machine.
- **Gold comic display type w/ blue outline** for mode labels and celebratory copy; yellow "NEXT ROUND!" energy.
- The **spring/screwdriver-T + bouncing "2"-ball** logo motion — implement as SVG paths + spring-physics keyframes (or Rive); it's the one animation worth the effort.
- **8-slot letter-ball loading bar** spelling LOADING — cheap, charming, do it.
- Gold pinstripe HUD edge.

**Update**
- Kill baked drop shadows and 1px bitmap outlines: replace with `filter: drop-shadow()` at 1 blur / 30% alpha; scale everything to `em`-based sizes so the layout goes fluid (design tokens: ball = `--ball: clamp(44px, 7vw, 60px)`, slot = 1.15 × ball).
- Flatten bevels to 2-stop gradients + one inset white hairline (`inset 0 1px 0 rgba(255,255,255,.45)`); the 2008 5-layer gloss reads as mud on retina.
- Balls: pure CSS — `radial-gradient(circle at 32% 26%, #fff 0 12%, #ececec 45%, #c9ccd3 78%, #9aa0ad 100%)` + 1px `#7a7f8c` rim; letter in a rounded geometric sans (Nunito ExtraBold / Baloo 2) at `#1D4ED8`, slightly larger cap-height than original for readability.
- Panels: keep `#0033BA` but as `rgba(0,51,186,.55)` + `backdrop-filter: blur(8px)` — modern glass, same color read.
- Mode-button glow halos: halve them; use `box-shadow: 0 0 0 2px gold, 0 8px 24px cyan/30%`.
- Disabled state: 40% opacity + `saturate(.2)` instead of the gray repaint.
- Replace shimmer-sweep popups with CSS `@keyframes` mask sweep (one element, GPU-friendly); keep the gold double border.
- Accessibility: original is mouse-only and low-contrast in slots — add focus rings (2px `#FDE047` offset 2px) and bump slot-ball contrast ~15% (ball `#2C5BD0` on tile `#0033BA`).
- All bitmap alphabets/particles → SVG sprite or webfont; the A–Z sheets exist only because 2008 had no text-in-engine.

**Do not carry over**: publisher splash, gray dead bevel chrome, black outlines on small text, Dutch dev placeholder strings, and the ultra-thin 1px white menu rect (use a proper 2px `rgba(255,255,255,.35)` glass frame instead).

*Not analyzed (time): `crosswordscene/background.png`, blitz/trophy scene assets; both follow the same blue-panel system per their identical panel sprite names.*
