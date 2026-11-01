# AGENTS.md

Working notes for whoever changes this repository next. `README.md` says what the parser does,
`docs/API.md` gives the configuration, the tree and the syntax, and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
apply.

## Verification and benchmarks

CI (`.github/workflows/ci.yml`) calls the catalog's shared
[`cmake.yml`](https://github.com/Kronuz/cpp-libraries/blob/main/.github/workflows/cmake.yml) on every push and nightly:
CTest on Linux (x86-64 GCC and Clang, arm64), macOS (arm64, x86-64) and FreeBSD, then again under
ASan+UBSan and TSan. The shared method is the catalog's
[PERFORMANCE.md](https://github.com/Kronuz/cpp-libraries/blob/main/docs/PERFORMANCE.md).

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
# ASan+UBSan as CI's linux-asan-ubsan job runs it (Linux, Clang)
CC=clang CXX=clang++ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
cmake --build build-asan && ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure
# libFuzzer for 600 s (an LLVM clang, not Apple's): corpus in build-fuzz/corpus, crashes build-fuzz/crash-*
CLANG=$(brew --prefix llvm)/bin fuzz/run.sh 600
```

The oracle is Xapian 2.1's query parser, frozen in `test/vectors/`: each row holds a query, its
flags and settings, and the `Query::get_description()` text or error message Xapian gave. The
program that recorded them links Xapian (GPL) and stays out of this MIT repository, so the rows
are the reference and are not regenerated here. `vectors` runs the 528 rows of `parse.jsonl`;
`t2`, `t3` and `t5` run `parse-v2.jsonl` (3,488 rows of Xapiand's configurations),
`parse-v3.jsonl` (1,995) and `parse-ab.jsonl` (706 rows where the parser and Xapian 2.0 differ,
each with its decided output). Every row the parser does not match is listed with its reason in
the matching `EXCEPTIONS*.md`; a new failure or an unused exception fails the test. `deep` parses
10^6 levels of nesting on a 512 KiB stack, `oom` fails each allocation of each query in turn
(skipped under TSan, whose runtime owns `operator new`), and `random` parses 1 MiB of random
bytes. The fuzz build instruments the library and its dependencies with ASan and UBSan and uses
`fuzz/queryparser.dict`; `run.sh` refuses to run if the library lacks coverage hooks. There is
no bench target: the parser makes no timing claim.
