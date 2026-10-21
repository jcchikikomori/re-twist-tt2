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

1. **Launch warning.** On boot, before the splash, a launch without a verified import shows a dialog on a
   black screen (OP-9, revised after the first Windows test: asking at the menu meant the logo and loading
   screens played with placeholder art first). The welcome flow waits until it closes. The dialog asks:

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
| Android | `<files>/re-twist-tt2/imported-assets/` | `<files>/re-twist-tt2/` |

On Android `<files>` is the app's private files folder (`/data/user/0/com.jcchikikomori.retwisttt2/files`). Other
apps can't read it, and uninstalling the app removes it.

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
- **A folder that won't move is updated in place.** Each rename is tried 5 times, 200 ms apart. On Windows an
  antivirus scan of the new files, an Explorer window on the folder, or a `.old` that would not delete can still
  hold it; then the verified files are copied over the old ones and the stamp is written last. A copy that fails
  part way keeps the old stamp, so the game asks for the import again. Re-importing used to fail there until the
  player deleted `imported-assets` by hand.
- `scripts/import-original.sh` is now a thin wrapper around `tt2 --import`.

### Verification

- Unit tests: SHA-256 vectors, the manifest and exclusion rules, the import job stage by stage, the prompt flow and
  the SETTINGS row.
- CI (`scripts/ci-import-smoke.sh`, Linux, Windows, and both packages): a synthetic install made of random bytes,
  with decoy DRM files, is imported from the terminal, the launch prompt and SETTINGS, and checked with
  `tt2-assets verify`. No CI run touches the real per-user folder.

## Android (as built, 2026-10-17)

The same manifest, the same checks and the same stamp, but the player brings a **ZIP** of their Text Twist 2
folder instead of pointing at the folder.

### The Flow on Android

1. **Launch warning.** The same dialog and the same "Original game required" text, followed by a line on making
   the ZIP. It is a native Android dialog (`importer/ImportDialog.kt`) over the game, not a drawn one, and its
   buttons are **Choose ZIP** and **Not now**.
1. **ZIP picker.** **Choose ZIP** opens the system's document picker (`ACTION_OPEN_DOCUMENT`, ZIP types only).
   Backing out of the picker returns to the dialog. With no app that can open a ZIP, the dialog says to install a
   file manager.
1. **Verify, then copy.** `importer/ZipImporter.kt` runs the desktop's passes inside the ZIP: hash every manifest
   entry, copy them to `imported-assets.staging`, hash the copies, write the stamp last, then swap the folders the
   same way. A ZIP of the folder (`Text Twist 2/...`) and a ZIP of its contents both work: the install root is
   wherever `properties/resources.xml` sits. The game then reloads its art without a restart.

SETTINGS → **IMPORT** opens the same dialog without the warning. Every message comes from
`ui/include/tt2ui/import_prompt.hpp` through JNI (`ui/src/android_import.cpp`), so both platforms say the same
thing; `ImportTextTest` fails if import text is copied into the Kotlin sources or `res/`.

### Safety on Android

- **Files go to the paths the manifest names**, never to the names inside the ZIP, so no entry can be written
  outside the staging folder ("zip slip"). An archive holding an unsafe name (absolute, `..`, a drive letter) is
  rejected outright anyway.
- **Only manifest entries are opened**, and the manifest can't list DRM, code or dictionary files.
- **Size caps:** the picked file is copied into the app's cache first (at most 1 GiB), and inside it each file may
  be at most 16 MiB and all of them 256 MiB. The real game is about 24 MB. The cached copy is deleted when the
  import ends, whatever the result.
- **Cancel** stops the copy; the previous import, if any, stays.
- A file that isn't a usable ZIP fails with "That file is not a usable ZIP archive. Nothing was copied."

### Verification on Android

- Kotlin unit tests (`./gradlew testDebugUnitTest`, CI's `android` job): the manifest rules, the importer pass by
  pass, the size caps, unsafe names, cancelling, and the swap. Every ZIP is made of synthetic bytes.
- `ui/tests/import-contract/` is shared by the C++ and the Kotlin tests: a synthetic manifest and the stamp
  `serialize_stamp()` writes for it. A folder Kotlin imports must pass C++'s `import_status()`.
- Checked by hand on a Galaxy Tab A7 Lite (Android 14) with a ZIP of a legitimately owned install.
