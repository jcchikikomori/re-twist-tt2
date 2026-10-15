# Asset and Third-Party Credits

What ships with re-twist-tt2 that someone else made, and under which license. The dictionaries have their own
page: [dictionary-sources.md](../dictionary-sources.md).

## Third-Party Code

| Component | Version | License | Where | Used for |
| --- | --- | --- | --- | --- |
| [tinyfiledialogs](https://sourceforge.net/projects/tinyfiledialogs/) by Guillaume Vareille | 3.21.5 | zlib | `third_party/tinyfiledialogs/` | The "choose your Text Twist 2 folder" dialog when importing the original assets |
| [SDL2, SDL2_image, SDL2_mixer](https://www.libsdl.org/) | system / MSYS2 package | zlib | linked, not vendored | Window, input, images, audio |

- tinyfiledialogs is vendored **unaltered**. `third_party/tinyfiledialogs/VERSION` records the upstream commit and
  the SHA-256 of each file; `LICENSE` holds its notice.
- It is compiled as its own C target, outside the project's warning flags (`CMakeLists.txt`, `tt2_tinyfd`).

## Art, Fonts and Sounds

- **Shipped:** none from Text Twist 2. The default build draws procedural placeholders.
- **Imported by the player:** the original art, fonts and sounds come from the player's own install, copied on
  their machine by `tt2 --import` or the in-game importer (`docs/design/import-assets.md`). They are never
  committed or packaged. `data/original-assets.sha256` holds only their relative paths and SHA-256 hashes.
