# Dictionary sources

The open (license-clean) dictionaries shipped in `public/data/` are built by
`scripts/build-open-dictionary.sh` from the sources below. They are the
fallback used when the user hasn't imported the copyrighted GameHouse originals.

## Full dictionary — `public/data/open-full.tree`

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

## Common dictionary — `public/data/open-common.tree`

Built as **ENABLE2k ∩ (3–8 letters) ∩ top-50k English frequency**, so the
playable racks/slots stay free of obscure Scrabble-only words.

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
- **Word count:** 22,460 (3–8 letters). 15,292 words have ≥15 sub-anagrams
  (anagram hubs), well above the 200-hub minimum.

## Word links (browser verification)

Every word can be verified against Wiktionary using the per-word URL template:

```
https://en.wiktionary.org/wiki/{word}
```

Spot-check (2026-10-11, `curl -L`, all returned HTTP 200):

| Word | Status | Notes |
|---|---|---|
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
scripts/build-open-dictionary.sh
```

Downloads are cached under `data-src/` (git-ignored). The script is idempotent:
re-running it produces byte-identical `.tree` files.

[enable2k]: https://everything2.com/title/ENABLE+word+list
[enable2k-readme]: https://raw.githubusercontent.com/BartMassey/wordlists/main/README-enable2k.txt
[freq]: https://github.com/hermitdave/FrequencyWords
