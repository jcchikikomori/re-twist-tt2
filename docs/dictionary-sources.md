# Dictionary sources

The open (license-clean) dictionaries shipped in `data/` are built by
`scripts/build-open-dictionary.sh` from the word lists in `data-src/` (see [Regenerating](#regenerating)). They are
the fallback used when the user hasn't imported the copyrighted GameHouse originals.

## Full dictionary — `data/open-full.tree`

- **Source:** [ENABLE2k (Enhanced North American Benchmark LExicon, 2000 edition)][enable2k]
- **Primary download:** <https://coursera.cs.princeton.edu/algs4/assignments/boggle/files/dictionary-enable2k.txt>
  (Princeton's *Algorithms* course mirror, plain ASCII, uppercase, one word per line)
- **Fallback download (gzipped):** <https://raw.githubusercontent.com/BartMassey/wordlists/main/enable2k.txt.gz>
- **License:** **Public domain.** The official release statement
  ([`README-enable2k.txt`][enable2k-readme]) reads, in part: *"The ENABLE master
  word list, WORD.LST, is herewith formally released into the Public Domain.
  Anyone is free to use it or distribute it in any manner they see fit."*
- **Access date:** 2026-10-11
- **Word count:** 173,528 (2–28 letters). After normalization (lowercase,
  a–z only, dedupe, sort) the `.tree` holds 173,528 words.
- **Files:** the download is `data-src/enable2k.raw.txt`; the normalized list is `data-src/enable.words`.
- **Blocklist:** none. The full list only validates guesses, so it keeps every word of the
  [blocklist](#blocklist--data-srcblocklistwords).

## Common dictionary — `data/open-common.tree`

Built as **ENABLE2k ∩ (3–8 letters) ∩ top-50k English frequency**, minus the
[blocklist](#blocklist--data-srcblocklistwords), so the playable racks/slots stay free of obscure Scrabble-only words
and of slurs and vulgar words.

- **Validity source:** ENABLE2k (public domain) — see above.
- **Commonness source:** [Hermit Dave's FrequencyWords][freq], `en_50k.txt`
  (the 50,000 most frequent English words from movie/TV subtitle corpora).
  - **Download:** <https://raw.githubusercontent.com/hermitdave/FrequencyWords/master/content/2018/en/en_50k.txt>
  - **Format:** `word <count>` per line.
- **License:** **CC BY-SA 4.0** for the content (the repo's README states
  *"MIT License for code. CC-by-sa-4.0 for content."*). The resulting
  `open-common.tree` is a selection of public-domain ENABLE2k words whose
  membership is filtered by this CC BY-SA frequency data; treat it as
  CC BY-SA 4.0.
- **Access date:** 2026-10-11
- **Files:** the download is `data-src/en_50k.raw.txt`; the selection before the blocklist is `data-src/common.words`
  (22,460 words).
- **Word count:** 22,360 (3–8 letters): 22,460 minus 100 blocked words. 19,181 of them have 5–8 letters (the rack
  pool), and 15,233 of those have ≥15 sub-anagrams (anagram hubs, the Classic bag's playability test), well above
  the 200-hub minimum. Before the blocklist the counts were 19,256 and 15,292.

## Blocklist — `data-src/blocklist.words`

`open-common.tree` leaves out the 100 words in `data-src/blocklist.words`, so none of them is ever a rack, an answer
slot, a Word of the Day or a Crossword root. `open-full.tree` keeps every one: typing a blocked word that the rack
can spell still counts, the same way the original game's guess list accepts words its common list never shows.

- **What it filters:** slurs (ethnic, racial, religious, homophobic, and disability slurs), sexual vulgarity, and
  strong profanity. The frequency list is built from film and TV subtitles, so plenty of these made its top 50,000.
- **The rule:**
  - Block a word whose every common sense is a slur, sexual vulgarity, or strong profanity (`fuck`, `cunt`,
    `whore`, `shitty`, `kike`).
  - Block a severe slur even when it has a rarer ordinary sense (`chink` a narrow gap, `coon` a raccoon, `fag` a
    cigarette, `dyke` a spelling of dike, `retard` to slow down, `pussy` a cat, `tits` the birds). The ordinary sense
    still counts when typed; it just never becomes a slot.
  - ENABLE2k leaves out capitalized words, so its `jew` and `mick` are there only for their slur senses (the verb
    "to haggle", the slur for an Irishman). `gyp` ("to cheat") is a slur built on "Gypsy", and `welsh` ("to welsh
    on a bet") one built on "Welsh". All four are blocked.
  - Keep a word whose ordinary sense is common and whose rude sense is mild (`ass` the animal, `cock` the bird,
    `prick`, `dick`, `tit`, `screw`), mild swearing (`damn`, `crap`, `arse`, `bugger`), and clinical or neutral
    terms (`penis`, `vagina`, `lesbian`, `queer`, `sex`).
- **Hand-made:** the words were picked by hand from `data-src/common.words`, not copied from another list, so the
  blocklist brings in no new license.
- **Format:** one lowercase word per line, sorted in byte order (`LC_ALL=C`); lines starting with `#` are comments.
  Every word must be in `data-src/common.words`: the build script and the tests both fail on an entry that filters
  nothing.
- **The raw list stays raw:** `data-src/common.words` keeps the blocked words. The filter runs only in the build
  step, so the frequency selection and the blocklist can each be checked on their own.

After editing the blocklist:

1. Run the build script (see [Regenerating](#regenerating)).
1. Update the counts above and the ones pinned in `engine/tests/test_open_dictionary.cpp`.
1. If the 5–8 letter pool changed size, update it in the Word of the Day notes too: `engine/include/tt2/bag.hpp`,
   `docs/reverse-engineering/game-scene.md` §8.2 and `docs/design/wotd-crossword.md`. The pool is shuffled by
   index, so removing a word changes the Word of the Day for many dates; recheck the 2026-10-22 example
   (`analogue`) there.

## Word links (browser verification)

Every word can be verified against Wiktionary using the per-word URL template:

```text
https://en.wiktionary.org/wiki/{word}
```

Spot-check (2026-10-11, `curl -L`, all returned HTTP 200):

| Word | Status | Notes |
| --- | --- | --- |
| rope | 200 | |
| cede | 200 | |
| clouds | 200 | |
| brits | 200 | |
| awarding | 200 | |
| ugh | 200 | |
| hear | 200 | |
| sickly | 200 | |
| fuchsia | 200 | unusual |
| zephyr | 200 | unusual |

The UI layer renders these links for the "correct word" (clickable to open in a
browser); the data side only needs to know the template above.

## Regenerating

```bash
cmake --build build --target tt2-tree
scripts/build-open-dictionary.sh build/tt2-tree
```

The script needs only `tt2-tree` and POSIX tools. It downloads nothing: the raw downloads
(`data-src/enable2k.raw.txt`, `data-src/en_50k.raw.txt`) are committed. In order, it:

1. Checks that `data-src/enable.words` and `data-src/common.words` still match those raw files.
1. Re-encodes `enable.words` and fails if the result differs from `data/open-full.tree`. It never rewrites that file.
1. Checks the blocklist: lowercase a–z words, all in `common.words`.
1. Writes `data/open-common.tree` (`common.words` minus the blocklist) and runs `tt2-tree verify` on it.

Re-running it produces byte-identical `.tree` files. `engine/tests/test_open_dictionary.cpp` (part of `ctest`) checks
the committed trees against `data-src/`, so an edit without a re-run fails the tests.

[enable2k]: https://everything2.com/title/ENABLE+word+list
[enable2k-readme]: https://raw.githubusercontent.com/BartMassey/wordlists/main/README-enable2k.txt
[freq]: https://github.com/hermitdave/FrequencyWords
