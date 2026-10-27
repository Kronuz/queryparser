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
| `a b` | | the default operator (`or_`, or `and_` through `config::default_op`) |
| `a AND b`, `OR`, `NOT`, `XOR`, `AND NOT`, `( )` | `boolean` | precedence XOR < AND/NOT, OR lowest |
| `a NEAR b`, `a ADJ b`, `NEAR/n`, `ADJ/n` | `boolean` | window n + terms - 1, default n 10 |
| `"a b"`, `a-b`, `a:b`, `a/b` | `phrase` | `PHRASE` over the words |
| `+a`, `-a` | `lovehate` | required and excluded items: `(R AND_MAYBE O) AND_NOT X` |
| `word*` | `wildcard` | `WILDCARD SYNONYM word`, no position |
| the last word, at the end of the text | `partial` | `(WILDCARD SYNONYM word OR word@n)`, for words of two or more characters |
| `word~`, `word~n` at the end | `fuzzy` | `EDIT_DISTANCE SYNONYM word~2` (or `~n`) |
| runs of CJK, Thai, ... | `cjk_ngram` | unigrams and bigrams, ANDed at one position, never stemmed |
| `NOT a`, a lone `-a` | `pure_not` | `(0 * <alldocuments> AND_NOT a)` |
| `a and b` | `boolean_any_case` | `and`, `or`, `not`, `xor` in any case are operators |

When NEAR or ADJ meets a word that `wildcard` or `partial` would expand, the whole text is
read again as plain words (no `boolean`, `wildcard` or `partial`).

`config::resolver` maps a field name before `:` to a term prefix (`title:obama` becomes
`Sobama`); without it `title:obama` is the phrase `title obama`.

## Status

Steps 1 and 2 of the plan in strata `docs/design/queryparser.md`: every flag above, the
stemming strategies, errors, and all 528 oracle rows of `parse.jsonl`. Stop words, default
prefixes, glob wildcards (`?`), the other default operators and expansion limits come with
step 3 (Xapiand's configurations); the extensions (escaping, boosts, slop, lenient errors) after.

## Tests

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

- `queryparser_vectors`: the oracle rows in `test/vectors/parse.jsonl`, with exceptions in
  `test/vectors/EXCEPTIONS.md`.
- `queryparser_test`: grammar tests. `queryparser_deep`: 10^6-level nesting on a 512 KiB stack.
- `queryparser_random`: 1 MiB of random input. `QUERYPARSER_FUZZ=ON` adds the libFuzzer target.

## License

MIT. No GPL source was used: behaviour comes from recorded vectors and public documentation.
