# Dictionary licenses

The source code of this project is MIT-licensed (see `../LICENSE`). The
dictionaries in this directory are **not** covered by the MIT license; each
one carries the license of the word lists it was built from. Sources and
access dates are in `../docs/dictionary-sources.md`.

| File | License |
| --- | --- |
| `open-full.tree` | Public domain |
| `open-common.tree` | CC BY-SA 4.0 |

## `open-full.tree` — public domain

Built from **ENABLE2k** (Enhanced North American Benchmark LExicon, 2000
edition). Its release statement reads: *"The ENABLE master word list,
WORD.LST, is herewith formally released into the Public Domain. Anyone is
free to use it or distribute it in any manner they see fit."*

To the extent possible under law, the `.tree` encoding of this list is also
dedicated to the public domain under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).

## `open-common.tree` — CC BY-SA 4.0

`open-common.tree` is a selection of ENABLE2k words (3–8 letters) that also
appear in the 50,000 most frequent English words of **FrequencyWords** by
Hermit Dave. The frequency data is licensed under CC BY-SA 4.0, so this
derived word list is distributed under the same license.

- **Attribution:** word selection derived from
  [FrequencyWords](https://github.com/hermitdave/FrequencyWords) by
  Hermit Dave (`content/2018/en/en_50k.txt`), CC BY-SA 4.0; words from
  ENABLE2k (public domain).
- **License:** [Creative Commons Attribution-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/)
  ([legal code](https://creativecommons.org/licenses/by-sa/4.0/legalcode)).
- **Changes:** filtered to words that are in ENABLE2k, are 3–8 letters long,
  and appear in `en_50k.txt`; lowercased, deduplicated, sorted, and encoded
  in the `.tree` format (`../docs/reverse-engineering/`).
- **Share-alike:** if you modify and redistribute this file, you must release
  your version under CC BY-SA 4.0 (or a compatible license).

No warranties are given. The license may not give you all the permissions
you need for your intended use.

## Not covered here

The original GameHouse dictionaries (`common.tree`, `full.tree`) are
copyrighted, gitignored, and never distributed with this project.
