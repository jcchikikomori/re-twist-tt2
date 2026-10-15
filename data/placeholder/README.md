# Generated placeholder art

Everything here is written by `tools/gen_placeholders.py`; do not edit it by hand. The game
loads these files only when the player has not imported their own Text Twist 2 install.

- **Shapes:** drawn by the script. No original GameHouse pixels; only colour values were sampled.
- **The port's own buttons:** `images/selectionscene/button_improvements.png` (the main-menu
  IMPROVEMENTS badge), `images/gui/button_arrow_left.png` / `button_arrow_right.png`, and
  `images/gui/button_import.png` (SETTINGS' IMPORT). These names don't exist in the original
  game, so they are used even after an import.
- **Fonts:** bitmap renderings of open fonts from `third_party/fonts/`:
  - Luckiest Guy (Apache 2.0, `LICENSE-LuckiestGuy.txt`): font_uibar, font_dialog_headers,
    font_dialog_headers2, font_uibar_labels.
  - Arimo Bold (SIL OFL 1.1, `OFL-Arimo.txt`): font_settingstext, font_uibar_numbers,
    font_nextround02, font_tutorial01, font_highscore_text, and the button labels.
  - Arimo Regular (the same file, weight 400): arial12 (FONT_TOOLTIP).
- **`2x/`:** the same fonts and images drawn again at twice the size (not resized), for GPU
  renderers at high resolutions. Fonts there are only PNG layers; they use the 1x XML.
  `background.png` has no 2x copy.
- **License of the rest:** MIT, like the code.
