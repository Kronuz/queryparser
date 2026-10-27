# T2 exceptions

One line per V2 row the parser does not match: `- ROW | KIND | query: reason`, ROW the 1-based line of parse-v2.jsonl.

Kinds:

- `unreachable`: FLAG_WORD_BREAKS needs ICU, which the oracle build lacked (6 rows); the runner excepts them by itself.
- `xapian-2.1`: Xapian 2.1 differs from the 2.0 reference (Q5). None found.
- `stemmer`: V2 used Xapian::Stem and the library uses snowball. None found.
- `open`: a row that fails and is tracked. It counts as failed, is listed on every run, and does not fail the run;
  a new failure, or an open row that now matches, does. The shapes are described in docs/status/queryparser.md.

- ROW | KIND | query: reason`, ROW the 1-based line of parse-v2.jsonl.
Kinds: `xapian-2.1` (2.1 differs from the 2.0 reference, Q5), `stemmer` (V2 used Xapian::Stem, the library uses snowball), `unreachable` (word breaks without ICU; excepted by the runner).

- 448 | open | -"a b" c: tracked
- 467 | open | -a OR b: tracked
- 1173 | open | (a "b c*: tracked
- 1714 | open | -a OR b: tracked
- 2311 | open | a~-1: tracked
- 2551 | open | 中文AND分词: tracked
- 2552 | open | "中文分词": tracked
- 2570 | open | "中文分词": tracked
- 2682 | open | ba~-1: tracked
- 2750 | open | ba~2 bc~1*: tracked
- 2793 | open | ba*~ OR~ bc*~: tracked
- 3056 | open | a~ OR~ b~: tracked
