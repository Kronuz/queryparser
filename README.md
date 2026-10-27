# queryparser

[![CI](https://github.com/Kronuz/queryparser/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/queryparser/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

A query-string parser for C++20. Text such as `+barack -"michelle obama" title:44th NEAR/3 president`
becomes a tree of terms and operators, with words analysed by the same rules as
[analysis](https://github.com/Kronuz/analysis) so query terms equal index terms.

The parser is iterative. Nesting depth is bounded by memory, never by the call stack, and
parsing never throws on input.

## Usage

```cpp
#include "queryparser.hh"

snowball::stemmer english("english");
queryparser::config cfg;          // flags: boolean | phrase | lovehate
cfg.stemmer = &english;           // stem_strategy::some by default
auto r = queryparser::parse("barack AND (michelle OR obama) -\"first lady\"", cfg);
if (!r) { /* r.err->message, r.err->offset */ }
r.tree.describe();                // Query(((Zbarack@1 AND (Zmichell@2 OR Zobama@3)) AND_NOT ...))
```

`tree` is a flat arena: `tree.root()`, `tree.at(id)` (a `node` with `k`, `pos`, `window`,
`first`, `next`) and `tree.text(node)`. `describe()` prints the format of Xapian's
`Query::get_description()`, which is how the oracle vectors compare.

## Syntax

| Syntax | Flag | Result |
|---|---|---|
| `a b` | | the default operator (`config::default_op`: OR, AND, XOR, or one of NEAR, PHRASE, ELITE_SET, SYNONYM, MAX over the words) |
| `a AND b`, `OR`, `NOT`, `XOR`, `AND NOT` | `boolean` | OR lowest, then XOR, then AND/NOT; juxtaposed items bind tighter than all of them |
| `( )` | | groups; recognised with or without `boolean` |
| `a NEAR b`, `a ADJ b`, `NEAR/n`, `ADJ/n` | `boolean` | window n + terms - 1, default n 10; outside a chain of plain words they are plain words |
| `"a b"`, `a-b`, `a:b`, `a/b` | `phrase` | `PHRASE` over the words |
| `+a`, `-a` | `lovehate` | required and excluded items: `(R AND_MAYBE O) AND_NOT X` |
| `word*` | `wildcard` | `WILDCARD SYNONYM word`, one position |
| `b?a`, `ba*c*`, `ba**` | `wildcard_single`, `wildcard_multi` | `WILDCARD SYNONYM` over the glob pattern |
| the last word, at the end of the text | `partial` | `(WILDCARD SYNONYM word OR word@n)`, for words of two or more characters |
| `word~`, `word~n` at the end | `fuzzy` | `EDIT_DISTANCE SYNONYM word~2 fixed_prefix_len=N`, N the bytes of the term prefix |
| runs of CJK, Thai, ... | `cjk_ngram` | unigrams and bigrams, ANDed at one position, never stemmed |
| `NOT a`, a lone `-a` | `pure_not` | `(0 * <alldocuments> AND_NOT a)` |
| `a and b` | `boolean_any_case` | `and`, `or`, `not`, `xor` in any case are operators |

Items that sit side by side form a group. Left operands flatten (`(a AND b) AND c` is one AND;
`a AND (b AND c)` is not), and the words of a group that are stop words (`config::stopper`) go
unless the group holds only stop words. A mixed NEAR/ADJ chain, an expanding operand of a chain,
or a stray `)` after an operator reads the whole text again as plain words (no `boolean`,
`wildcard`, `partial` or `fuzzy`; brackets stay).

`config::default_prefix` is put before every term of a word that has no field;
`config::{partial,wildcard,fuzzy}_expansion` are the caps (`max`, `limit`) that wildcard and
edit-distance nodes carry for the engine that expands them.

`config::resolver` maps a field name before `:` to a term prefix (`title:obama` becomes
`Sobama`); without it `title:obama` is the phrase `title obama`.

## Status

Steps 1 to 3 of the plan in strata `docs/design/queryparser.md`: every flag above, the stemming
strategies, stop words, default prefixes, the other default operators, expansion caps, errors, all
528 rows of `parse.jsonl` (T1) and Xapiand's configurations in `parse-v2.jsonl` (T2: 3,488 rows; the
rows where Xapian 2.1, the oracle, differs from the 2.0 that Xapiand ships are listed in
`test/vectors/EXCEPTIONS-v2.md`). Where the shipped Xapian 2.0 and the 2.1 oracle differ, the
parser takes the better behaviour: a capitalized word is stemmed under `some` like any word, `ab+` and
`c#` keep their suffix (the index keeps it), and `NEAR` beside a wildcard with BOOLEAN off is a word.
A lone exclusion (`-a`) under BOOLEAN with no stop list is a syntax error, where 2.0 reads it as `a`
and turns the exclusion into an inclusion. The extensions (escaping, boosts, slop, lenient errors)
come after.

## Tests

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

- `queryparser_vectors`: the oracle rows in `test/vectors/parse.jsonl`, with exceptions in
  `test/vectors/EXCEPTIONS.md`. `queryparser_t2`: the rows of `parse-v2.jsonl` under the settings each
  records, with `test/vectors/EXCEPTIONS-v2.md`; the same program runs `parse-v3.jsonl` (T3) and
  `parse-ab.jsonl` (T5: the rows where an A/B run inside Xapiand found the parser differing from 2.0,
  with the decision for each as expected output and the 2.0 answer where they differ).
- `queryparser_test`: grammar tests. `queryparser_deep`: 10^6-level nesting on a 512 KiB stack.
- `queryparser_random`: 1 MiB of random input. `QUERYPARSER_FUZZ=ON` adds the libFuzzer target.

## License

MIT. No GPL source was used: behaviour comes from recorded vectors and public documentation.
