# Import contract

The Android importer (Kotlin, `android/app/.../importer/ZipImporter.kt`) and the game (C++,
`ui/include/tt2ui/asset_import.hpp`) must agree on what a finished import looks like. Both test suites read
these files, so a change on either side that breaks the other fails a test (T3.4 in the Android plan).

| File | What it is |
| --- | --- |
| `manifest.sha256` | A synthetic manifest: three made-up files whose bytes are their own path in UTF-8 (`fonts/contract.xml` holds the 18 bytes `fonts/contract.xml`). Nothing here comes from Text Twist 2. |
| `import-stamp.txt` | The golden stamp: what `tt2ui::serialize_stamp()` writes for that manifest. |
| `import-roots.txt` | The install folders a manifest entry may sit in, one per line, in order: `tt2::IMPORT_ROOTS` and Kotlin's `AssetManifest.IMPORT_ROOTS` must both equal it. Folder names only. |

- **C++** (`ui/tests/test_import_contract.cpp`): `serialize_stamp()` gives the golden bytes, and a folder laid
  out the way Kotlin writes it passes `import_status()`. `engine/tests/test_asset_manifest.cpp` checks
  `IMPORT_ROOTS` against `import-roots.txt` and that an entry under each root parses.
- **Kotlin** (`ImportContractTest.kt`): a ZIP of those files, imported with the golden stamp, gives exactly
  that layout, with the stamp copied byte for byte. `AssetManifestTest.kt` makes the same roots checks as C++.

Change these files only together with both sides.
