# queryparser

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/queryparser/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/queryparser/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

Query-string parser for C++20: Xapian-compatible search syntax into a flat tree, iterative and
never throwing on input. Text such as `+barack -"michelle obama" title:44th NEAR/3 president`
becomes a tree of terms and operators, with words analyzed by the same rules as
[analysis](https://github.com/Kronuz/analysis), so query terms equal index terms. Reach for it
to parse what users type into a search box, in front of a Xapian-style index.

## ✨ Highlights

- **Xapian-compatible.** It matches Xapian 2.1's query parser on 528 recorded oracle rows and
  on 3,488 rows of Xapiand's configurations; every row it differs on is listed with the
  reason.
- **The same words as the index.** Words go through analysis's word rule, stemmer and stop
  words, so a query term equals the term `analysis::term_generator` indexed.
- **The full syntax.** Boolean operators, `NEAR` and `ADJ`, phrases, `+required` and
  `-excluded`, field prefixes, wildcards, partial and fuzzy words, and CJK n-grams, each
  behind a flag.
- **Iterative.** Nesting depth is bounded by memory, never by the call stack; a test parses
  10^6 levels of nesting on a 512 KiB stack.
- **Never throws on input.** Errors come back as a kind, a byte offset and a message, and a
  failed allocation is `out_of_memory`.
- **A flat tree.** The query is an arena of nodes, and `describe()` prints it in the format of
  Xapian's `Query::get_description()`.
- **Fuzzed.** A libFuzzer target, and a 1 MiB random-input test that CI runs under ASan and
  UBSan.

## 🚀 Quick start

```cpp
#include "queryparser.hh"

#include <cstdio>

int main() {
	snowball::stemmer english("english");
	queryparser::config cfg;   // flags: boolean | phrase | lovehate
	cfg.stemmer = &english;    // stem_strategy::some by default
	auto r = queryparser::parse("barack AND (michelle OR obama) -\"first lady\"", cfg);
	if (!r) {
		std::printf("error at %zu: %s\n", r.err->offset, r.err->message.c_str());
		return 1;
	}
	std::printf("%s\n", r.tree.describe().c_str());
	// Query((Zbarack@1 AND ((Zmichell@2 OR Zobama@3) AND_NOT (first@4 PHRASE 2 lady@5))))
}
```

`r.tree` is a flat arena: `root()`, `at(id)` and `text(node)` walk it.
[docs/API.md](docs/API.md) has the configuration, the tree and the full syntax table.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer. CMake fetches
[analysis](https://github.com/Kronuz/analysis) at a pinned commit, which brings
[unicode](https://github.com/Kronuz/unicode) and [snowball](https://github.com/Kronuz/snowball).
CI builds it on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan,
and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(queryparser
  GIT_REPOSITORY https://github.com/Kronuz/queryparser.git
  GIT_TAG        8a9798f22065cba082db4b8e10df6e55d0902519)  # main tracks the tip
FetchContent_MakeAvailable(queryparser)

target_link_libraries(my_app PRIVATE queryparser::queryparser)
```

Then `#include "queryparser.hh"`.

## 🎯 When to use it

Use it to turn user-typed search queries into a tree for a Xapian index, or for any engine
that evaluates Xapian-style queries over terms from [analysis](https://github.com/Kronuz/analysis).

Not for:

- Escaping, boosts, slop or lenient error recovery: the grammar has none of them.
- Byte-for-byte Xapian 2.0 behavior: where Xapian 2.0 and 2.1 differ, it takes one side case
  by case ([docs/API.md](docs/API.md#differences-from-xapian)).
- Unbounded input: parsing takes up to 88 bytes of memory per input byte and the tree is as
  deep as a sixth of the text, so bound the text you pass and walk the tree without recursion.

## 📚 Documentation

- [docs/API.md](docs/API.md): `parse`, `config`, the tree, the syntax, the differences from
  Xapian, and how the parser is verified.
- [test/vectors/](test/vectors/): the recorded oracle rows and the exceptions files.
- [CHANGELOG.md](CHANGELOG.md): changes by date.
- [Catalog](https://github.com/Kronuz/cpp-libraries#whats-here): the rest of the family.

## 🛠️ Building and testing

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

`-DQUERYPARSER_FUZZ=ON` builds the libFuzzer target (Clang), with the whole tree instrumented.

## 🤝 Contributing

Read the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
before opening a pull request; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo. See [LICENSE](LICENSE). It contains no GPL
source: its behavior comes from recorded vectors and Xapian's public documentation.
