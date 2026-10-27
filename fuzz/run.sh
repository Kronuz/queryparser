#!/bin/sh
# Builds the instrumented fuzz target and runs it: fuzz/run.sh [seconds] [max_len]. Needs an LLVM
# clang with libFuzzer (not Apple clang); set CLANG to its directory. Output lands in $OUT.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
CLANG=${CLANG:-/usr/local/opt/llvm/bin}
OUT=${OUT:-$HERE/build-fuzz}
cmake -S "$HERE" -B "$OUT" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER="$CLANG/clang" \
  -DCMAKE_CXX_COMPILER="$CLANG/clang++" -DQUERYPARSER_FUZZ=ON ${CMAKE_ARGS}
cmake --build "$OUT" -j4 --target queryparser_fuzz
# The library must carry coverage hooks, or libFuzzer is blind to it (cov stays near 24).
n=$(nm "$OUT/libqueryparser.a" | grep -c sanitizer_cov || true)
[ "$n" -gt 0 ] || { echo "libqueryparser.a is not instrumented" >&2; exit 1; }
mkdir -p "$OUT/corpus"
"$OUT/queryparser_fuzz" -max_len="${2:-1048576}" -timeout=10 -max_total_time="${1:-600}" \
  -dict="$HERE/fuzz/queryparser.dict" -artifact_prefix="$OUT/crash-" "$OUT/corpus"
