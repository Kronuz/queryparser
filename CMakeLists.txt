cmake_minimum_required(VERSION 3.14)
project(queryparser LANGUAGES C CXX)

# The fuzz build instruments the whole tree (library, analysis, unicode, snowball) for
# coverage, ASan and UBSan; only the fuzz target links the libFuzzer driver.
option(QUERYPARSER_FUZZ "Build the libFuzzer target" OFF)
if(QUERYPARSER_FUZZ)
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -g -O1)
  add_link_options(-fsanitize=address,undefined)
endif()

include(FetchContent)
# analysis: the word rules (word_scanner), stop words and, through it, unicode and snowball.
FetchContent_Declare(analysis GIT_REPOSITORY https://github.com/Kronuz/analysis.git GIT_TAG 9b15abfb55e19cef668ffe0a3e84dbb62dd41054)
FetchContent_MakeAvailable(analysis)

add_library(queryparser STATIC queryparser.cc)
target_include_directories(queryparser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(queryparser PUBLIC cxx_std_20)
target_link_libraries(queryparser PUBLIC analysis::analysis)
add_library(queryparser::queryparser ALIAS queryparser)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  # Checked containers turn an out-of-range access into an abort the tests see.
  target_compile_definitions(queryparser PRIVATE _GLIBCXX_ASSERTIONS _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
  add_executable(queryparser_test test/test.cc)
  target_link_libraries(queryparser_test PRIVATE queryparser)
  add_test(NAME queryparser COMMAND queryparser_test)

  add_executable(queryparser_vectors test/vectors.cc)
  target_link_libraries(queryparser_vectors PRIVATE queryparser)
  add_test(NAME vectors COMMAND queryparser_vectors "${CMAKE_CURRENT_SOURCE_DIR}/test/vectors/parse.jsonl"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/test/vectors/EXCEPTIONS.md")

  add_executable(queryparser_deep test/deep.cc)
  find_package(Threads REQUIRED)
  target_link_libraries(queryparser_deep PRIVATE queryparser Threads::Threads)
  add_test(NAME deep COMMAND queryparser_deep)

  add_executable(queryparser_oom test/oom.cc)
  target_link_libraries(queryparser_oom PRIVATE queryparser)
  add_test(NAME oom COMMAND queryparser_oom)

  add_executable(queryparser_random test/random.cc)
  target_link_libraries(queryparser_random PRIVATE queryparser)
  add_test(NAME random COMMAND queryparser_random)

  if(QUERYPARSER_FUZZ)
    add_executable(queryparser_fuzz fuzz/fuzz_parse.cc)
    target_link_libraries(queryparser_fuzz PRIVATE queryparser)
    target_link_options(queryparser_fuzz PRIVATE -fsanitize=fuzzer)
  endif()
endif()
