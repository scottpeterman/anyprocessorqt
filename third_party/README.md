# Third-party code

| Directory | What | Version | License |
|---|---|---|---|
| `hunspell/` | Hunspell spell-checking library (sources from `src/hunspell`, built as a static library) | 1.7.2 | MPL 1.1 / GPL 2+ / LGPL 2.1+ (tri-license; used under GPL) — see `hunspell/COPYING*`, `license.*` |
| `dictionaries/en_US.*` | US English dictionary from SCOWL, as packaged by Debian (`hunspell-en-us` 2020.12.07) | 2020.12.07 | SCOWL permissive license — see `dictionaries/LICENSE-en_US.txt` |

Hunspell is unmodified. To update, copy `src/hunspell/*.{cxx,hxx}`, `hunspell.h` and `hunvisapi.h` from a release tag.
