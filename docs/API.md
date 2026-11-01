# queryparser API

Everything lives in `queryparser.hh`, in namespace `queryparser`.

## Parsing

`parse(text, config)` returns a `parse_result`: the query `tree` and an optional `err`
(`kind`, byte `offset`, `message`). It converts to `true` when parsing succeeded. Input never
makes it throw: a failed allocation becomes `error_kind::out_of_memory`, and only an
exception thrown by the resolver escapes.

`config` holds:

| Member | Default | Meaning |
|---|---|---|
| `flags` | `boolean \| phrase \| lovehate` | The syntax switches in [Syntax](#syntax). |
| `stem`, `stemmer` | `some`, none | The [analysis](https://github.com/Kronuz/analysis) stem strategy and a non-owning `snowball::stemmer*`. Without a stemmer, words are not stemmed. |
| `stop`, `stopper` | `stemmed`, none | The stop strategy and a non-owning `analysis::stopper*`. |
| `default_op` | `or_` | The operator between juxtaposed items: `or_`, `and_`, `xor_`, `and_maybe`, `near`, `phrase`, `elite_set`, `synonym` or `max`. |
| `default_prefix` | empty | Put before every term of a word that has no field. |
| `partial_expansion`, `wildcard_expansion`, `fuzzy_expansion` | no cap | The caps (`max`, `limit`) that wildcard and edit-distance nodes carry for the engine that expands them. |
| `resolver` | none | Maps a field name before `:` to a term prefix (`title:obama` becomes `Sobama`). Without it, `title:obama` is the phrase `title obama`. |
| `max_word_length` | 64 | Bytes after lowercasing; longer words are dropped, and a dropped word is no operand. |
| `max_term_bytes` | 2^32 - 1 | Total bytes of term text; a query over it fails with `too_long`. |

## The tree

`query` is a flat arena: `root()`, `at(id)` (a `node` with `k`, `pos`, `window`, `first` and
`next`), `size()` and `text(node)`. `describe()` prints the format of Xapian's
`Query::get_description()`, which is how the oracle vectors compare. An empty query has no
root and matches nothing.

The views `text()` returns live as long as the query object, and are invalid after it is
moved or copied from. Parsing takes up to 88 bytes of memory per input byte, and the tree is
as deep as a sixth of the text, so bound the text you pass and walk the tree without
recursion.

## Syntax

| Syntax | Flag | Result |
|---|---|---|
| `a b` | | the default operator (`config::default_op`: OR, AND, XOR, or one of NEAR, PHRASE, ELITE_SET, SYNONYM, MAX over the words) |
| `a AND b`, `OR`, `NOT`, `XOR`, `AND NOT` | `boolean` | OR lowest, then XOR, then AND/NOT; juxtaposed items bind tighter than all of them |
| `( )` | | groups; recognized with or without `boolean` |
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
`a AND (b AND c)` is not), and the words of a group that are stop words (`config::stopper`)
are dropped unless the group holds only stop words. A mixed NEAR/ADJ chain, an expanding
operand of a chain, or a stray `)` after an operator reads the whole text again as plain words
(no `boolean`, `wildcard`, `partial` or `fuzzy`; brackets stay).

## Differences from Xapian

The oracle is Xapian 2.1; Xapiand ships Xapian 2.0. Where the two differ, the parser takes
the better behavior of the two, case by case:

- A capitalized word is stemmed under `some`, like any word (2.0 does; 2.1 leaves it as
  written).
- `ab+` and `c#` keep their suffix, as the index keeps it.
- `NEAR` beside a wildcard with `boolean` off is a word.
- A lone exclusion (`-a`) under `boolean` with no stop list is a syntax error, where 2.0
  reads it as `a` and turns the exclusion into an inclusion.

`test/vectors/EXCEPTIONS*.md` lists every recorded row the parser does not match, with the
reason.

## How it is verified

- `queryparser_vectors`: the 528 oracle rows in `test/vectors/parse.jsonl`, with exceptions in
  `EXCEPTIONS.md`.
- `queryparser_t2`: the 3,488 rows of `parse-v2.jsonl` (Xapiand's configurations) under the
  settings each records, with `EXCEPTIONS-v2.md`. The same program runs `parse-v3.jsonl` and
  `parse-ab.jsonl`, the rows where an A/B run inside Xapiand found the parser differing from
  Xapian 2.0, with the decision for each as expected output.
- `queryparser_test`: grammar tests. `queryparser_deep`: 10^6 levels of nesting on a 512 KiB
  stack. `queryparser_random`: 1 MiB of random input.
- `-DQUERYPARSER_FUZZ=ON` adds the libFuzzer target (`fuzz/fuzz_parse.cc`).
