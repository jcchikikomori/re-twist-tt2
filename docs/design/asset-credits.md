# Asset and Third-Party Credits

What ships with re-twist-tt2 that someone else made, and under which license. The dictionaries have their own
page: [dictionary-sources.md](../dictionary-sources.md).

## Third-Party Code

| Component | Version | License | Where | Used for |
| --- | --- | --- | --- | --- |
| [tinyfiledialogs](https://sourceforge.net/projects/tinyfiledialogs/) by Guillaume Vareille | 3.21.5 | zlib | `third_party/tinyfiledialogs/` | The "choose your Text Twist 2 folder" dialog when importing the original assets |
| [SDL3, SDL3_image, SDL3_mixer](https://www.libsdl.org/) | 3.4.18, 3.4.8, 3.2.4 | zlib | system or MSYS2 package, or built from the release tarball (`TT2_FETCH_SDL3`); not vendored | Window, input, rendering, images, audio |
| [stb_image, stb_vorbis](https://github.com/nothings/stb) by Sean Barrett | as bundled in SDL3_image / SDL3_mixer | public domain or MIT | inside SDL3_image and SDL3_mixer | Decoding PNG / JPEG and Ogg Vorbis |

- tinyfiledialogs is vendored **unaltered**. `third_party/tinyfiledialogs/VERSION` records the upstream commit and
  the SHA-256 of each file; `LICENSE` holds its notice.
- It is compiled as its own C target, outside the project's warning flags (`CMakeLists.txt`, `tt2_tinyfd`).

## Art, Fonts and Sounds

- **Shipped:** none from Text Twist 2. `data/placeholder/` holds generated stand-ins for the fonts, the dialog
  frame and the dialog buttons, written by `tools/gen_placeholders.py`. The shapes are new; only the colour values
  were sampled from the original. Anything else still draws as a procedural placeholder in code.

| Font | License | Where | Used for |
| --- | --- | --- | --- |
| [Luckiest Guy](https://fonts.google.com/specimen/Luckiest+Guy) by Astigmatic | Apache 2.0 | `third_party/fonts/luckiest-guy/` | Header-style bitmap fonts (`font_uibar`, `font_dialog_headers*`, `font_uibar_labels`) |
| [Arimo](https://fonts.google.com/specimen/Arimo) by Steve Matteson | SIL OFL 1.1, no Reserved Font Name | `third_party/fonts/arimo/` | Text bitmap fonts and the button labels |

- Both are unaltered copies from the google/fonts repository; `third_party/fonts/README.md` records the commits.
- The bitmap sheets are renderings of these fonts, so their licence files ship next to them in `data/placeholder/`.
- **Imported by the player:** the original art, fonts and sounds come from the player's own install, copied on
  their machine by `tt2 --import` or the in-game importer (`docs/design/import-assets.md`). They are never
  committed or packaged. `data/original-assets.sha256` holds only their relative paths and SHA-256 hashes.
