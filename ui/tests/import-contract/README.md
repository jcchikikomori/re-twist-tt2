# Import contract

The Android importer (Kotlin, `android/app/.../importer/ZipImporter.kt`) and the game (C++,
`ui/include/tt2ui/asset_import.hpp`) must agree on what a finished import looks like. Both test suites read
these files, so a change on either side that breaks the other fails a test (T3.4 in the Android plan).

| File | What it is |
| --- | --- |
| `manifest.sha256` | A synthetic manifest: three made-up files whose bytes are their own path in UTF-8 (`fonts/contract.xml` holds the 18 bytes `fonts/contract.xml`). Nothing here comes from Text Twist 2. |
| `import-stamp.txt` | The golden stamp: what `tt2ui::serialize_stamp()` writes for that manifest. |

- **C++** (`ui/tests/test_import_contract.cpp`): `serialize_stamp()` gives the golden bytes, and a folder laid
  out the way Kotlin writes it passes `import_status()`.
- **Kotlin** (`ImportContractTest.kt`): a ZIP of those files, imported with the golden stamp, gives exactly
  that layout, with the stamp copied byte for byte.

Change these files only together with both sides.
