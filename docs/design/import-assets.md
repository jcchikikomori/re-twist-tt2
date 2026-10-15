# Text Twist 2 — Importing the assets

> The end-user should be responsible for importing the assets from the original game.
> On each launch, if the assets hasn't imported yet, will ask the end-user to import it.

## Missing Assets warning dialog

- On each launch (unless the assets are being imported), will ask the user to import from the original game's directory.
- The warning message should be like this: "This program requires you to import the source material from the original game. Otherwise, you won't enjoy the full experience. Besides, the placeholder assets are still on the way."
- The user won't ask again if the assets are imported & verified.

## Import Asset functionality

- Should base on the internal script file named: `scripts/import-original.sh`
- Verify the assets using Checksum (MD5, etc.), and the MD5 strings from the project will be used to check the imported assets.

## References

- https://github.com/hedge-dev/UnleashedRecomp

## Decisions (as built, 2026-10-15)

The work plan's open points, as approved, and what the code does today.

### The Flow

1. **Launch warning.** The first time the main menu shows without a verified import, a dialog asks:

   > **Original game required**
   >
   > This game needs the art, fonts and sounds from your own copy of Text Twist 2. The replacement placeholder art
   > isn't ready yet, so without them the game won't look or sound right. Import them from your Text Twist 2
   > folder now?

   It asks on every launch until an import is verified. NO keeps the placeholders for this session.
1. **Folder picker.** YES opens the system's folder dialog ([tinyfiledialogs](asset-credits.md), zlib). Where no
   graphic dialog exists (a headless session, or the Steam Deck's Game Mode), the game explains
   `tt2 --import "<folder>"` instead of falling back to a terminal prompt.
1. **Verify, then copy.** Every file is checked first; one missing or different file fails the import and copies
   nothing. The copies are checked again, then swapped in, and the art reloads without a restart.

Main menu → SETTINGS → **IMPORT** runs steps 2 and 3 again (a deliberate deviation from the original dialog,
`docs/reverse-engineering/help-and-settings.md` §3.5). `tt2 --import DIR` runs them from a terminal.

### Checksums

- **SHA-256, not MD5.** It costs the same to compute, and `sha256sum` (Linux) and `Get-FileHash` (Windows) print it
  by default, so players can check a file themselves.
- **`data/original-assets.sha256`** lists 304 files: relative paths and hashes only, never the files. It uses
  `sha256sum`'s format, so `sha256sum -c` inside an install checks it too. `tt2-assets manifest <install>` generated
  it from a legitimately owned install.
- **Never imported, never opened:** `texttwist2_RADRM.dll`, `*.nfo`, `*.dll`, `*.exe`, `*.tree`, and the caches
  Windows and macOS leave in folders (`Thumbs.db`, `desktop.ini`, `.DS_Store`). These rules decide from the file
  name alone.

### Where the Files Go

| System | Imported assets | Profiles and settings |
| --- | --- | --- |
| Linux, Steam Deck | `~/.local/share/re-twist-tt2/imported-assets/` | `~/.local/share/re-twist-tt2/` |
| Windows | `%APPDATA%\re-twist-tt2\imported-assets\` | `%APPDATA%\re-twist-tt2\` |

`imported-assets/import-stamp.txt` records which manifest the folder was verified against. On launch the game reads
it and checks the files exist, without hashing anything. A newer manifest marks the import as needing an update.

| Flag | Meaning |
| --- | --- |
| `--import DIR` | Import from the install at `DIR`, then exit (0 = imported, 1 = failed) |
| `--asset-dir DIR` | Use `DIR` instead of the per-user folder above |
| `--assets DIR` | Load the art from `DIR` as is: no import, no checks, no prompt |
| `--asset-manifest FILE` | Check imports against `FILE` instead of the bundled manifest |

### Safety

- **All or nothing.** The copy goes to `imported-assets.staging`, is verified, then replaces the folder through
  `imported-assets.old`. A failed or interrupted import keeps the previous one.
- **The game frees its loaded art and music before the swap**, because Windows can't rename a folder while a music
  file is open.
- `scripts/import-original.sh` is now a thin wrapper around `tt2 --import`.

### Verification

- Unit tests: SHA-256 vectors, the manifest and exclusion rules, the import job stage by stage, the prompt flow and
  the SETTINGS row.
- CI (`scripts/ci-import-smoke.sh`, Linux, Windows, and both packages): a synthetic install made of random bytes,
  with decoy DRM files, is imported from the terminal, the launch prompt and SETTINGS, and checked with
  `tt2-assets verify`. No CI run touches the real per-user folder.
