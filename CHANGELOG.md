# Changelog

All notable changes to `queryparser`, newest first. The library has no release tags yet, so
entries are dated.

## 2026-10-29

### Added

- The parser, covering steps 1 to 3 of the plan in strata's `docs/design/queryparser.md`: every
  flag, the stemming strategies, stop words, default prefixes, the other default operators,
  expansion caps and errors. It passes all 528 rows of `parse.jsonl` (T1) and Xapiand's
  configurations in `parse-v2.jsonl` (T2: 3,488 rows), with the rows where the Xapian 2.1 oracle
  differs from the Xapian 2.0 that Xapiand ships listed in `test/vectors/EXCEPTIONS-v2.md`.
- Where Xapian 2.0 and 2.1 differ, the parser takes the better behavior: a capitalized word is
  stemmed under `some` like any word, `ab+` and `c#` keep their suffix (the index keeps it), and
  `NEAR` beside a wildcard with BOOLEAN off is a word. A lone exclusion (`-a`) under BOOLEAN
  with no stop list is a syntax error, where 2.0 reads it as `a` and turns the exclusion into an
  inclusion.
