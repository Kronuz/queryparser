# Source layout

What each source file holds, for whoever opens the library to change one part of it.

| File | Lines | What it holds |
|---|---|---|
| `queryparser.hh` | 190 | The public API: flags, the tree (`node`, `query`), `config`, `error`, `parse()` |
| `queryparser.cc` | 1,295 | `parse()`: the node builder (`builder`, text pool and 32-bit limits) and the iterative parser (`parser`, its items, frames and pending operators) |
| `queryparser_describe.cc` | 140 | `query::describe()`: a tree as the text of Xapian's `Query::get_description()`, with its operator names and control-byte escaping |

`test/` holds one program per behavior (`test.cc` the unit cases, `vectors.cc` and `t2.cc` the
recorded Xapian rows, `deep.cc`, `oom.cc`, `random.cc`), with `jsonl.hh` and `tree_check.hh` as
shared support headers. `fuzz/fuzz_parse.cc` is the libFuzzer target.

## Kept whole over 1,000 lines

- `queryparser.cc` (1,295 lines). About 1,050 of them are the `parser` class: one state machine
  whose members share its scan position, frame stack, item list and pending marks, and call one
  another throughout a step (`step`, `word_group`, `quoted`, `close_group`, `combine_pass`,
  `reduce`). A class body cannot be cut, and a piece holding part of it would not compile or read
  alone. The `builder` (about 110 lines) stays beside it: the parser is its only user, it throws
  the file-local `limit_exceeded` that `parse()` catches, and moving it to a header would only
  add a file. `parse()`, the entry point, runs the parser and, when the text must be re-read as
  plain words, runs it again.
- `test/vectors/parse-v2.jsonl` (3,488 lines) and `parse-v3.jsonl` (1,995) are recorded data,
  frozen reference rows from Xapian 2.1, not source.

The description renderer is the one part that moved out: it reads only the public tree, shares
nothing with the parser, and is where a change to the description format (the text every test row
is compared against) is made.
