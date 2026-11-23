# Source layout

What each source file holds, for whoever opens the library to change one part of it.

| File | Lines | What it holds |
|---|---|---|
| `queryparser.hh` | 190 | The public API: flags, the tree (`node`, `query`), `config`, `error`, `parse()` |
| `queryparser.cc` | 60 | `parse()`: runs the parser and, when the text must be re-read as plain words, runs it again; turns the limits and allocation failures into errors |
| `queryparser/parser.hh` | 420 | The parser's internals: the node builder (`builder`, text pool and 32-bit limits), a group's items, frames and pending operators, and the `parser` class: its data members and one declaration per member, with its comment |
| `queryparser/parser_operators.hh` | 270 | The driver and operator recognition: the constructor, `run()`, `step()`'s dispatch, keywords, `AND`/`OR`/`NOT`/`XOR` (`binary_keyword`, `op_kind`, `op_label`), `NEAR`/`ADJ` words and their re-reads, a stray `)` |
| `queryparser/parser_words.hh` | 446 | The word layer: globs, fuzzy `~n` suffixes, word groups joined by generators, proximity chains, CJK n-grams, quoted phrases, stop words, and terms with their prefix and stem |
| `queryparser/parser_groups.hh` | 349 | Grouping and the tree: items and frames (`operand`, `push_item`, brackets), closing a group, the combine pass over its items, `join`, `nary`, `fold` and operator `reduce` |
| `queryparser_describe.cc` | 140 | `query::describe()`: a tree as the text of Xapian's `Query::get_description()`, with its operator names and control-byte escaping |

The `queryparser/` headers are internal: `queryparser.cc` includes `parser.hh`, which includes the
three layer headers after the class, so the parser is one translation unit and inlines as it did.
A wildcard or CJK bug opens `parser_words.hh`; a precedence or bracket bug opens
`parser_operators.hh` or `parser_groups.hh`. The class keeps seven one-line state accessors
(`needs_reparse()`, `boolean()`, `phrase()`, `lovehate()`, `has()`, `top()`, `off()`) beside their
declarations, since every layer reads them.

`test/` holds one program per behavior (`test.cc` the unit cases, `vectors.cc` and `t2.cc` the
recorded Xapian rows, `deep.cc`, `oom.cc`, `random.cc`), with `jsonl.hh` and `tree_check.hh` as
shared support headers. `fuzz/fuzz_parse.cc` is the libFuzzer target.

## Kept whole over 1,000 lines

- `test/vectors/parse-v2.jsonl` (3,488 lines) and `parse-v3.jsonl` (1,995) are recorded data,
  frozen reference rows from Xapian 2.1, not source.

The description renderer stands apart from the parser: it reads only the public tree, shares
nothing with the parser, and is where a change to the description format (the text every test row
is compared against) is made.
