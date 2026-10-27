cmake_minimum_required(VERSION 3.14)
project(queryparser LANGUAGES C CXX)

include(FetchContent)
# analysis: the word rules (word_scanner), stop words and, through it, unicode and snowball.
FetchContent_Declare(analysis GIT_REPOSITORY https://github.com/Kronuz/analysis.git GIT_TAG bb718c446f258f5e3a92e03cda6c0a029e051a6c)
FetchContent_MakeAvailable(analysis)

add_library(queryparser STATIC queryparser.cc)
target_include_directories(queryparser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(queryparser PUBLIC cxx_std_20)
target_link_libraries(queryparser PUBLIC analysis::analysis)
add_library(queryparser::queryparser ALIAS queryparser)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
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

  add_executable(queryparser_random test/random.cc)
  target_link_libraries(queryparser_random PRIVATE queryparser)
  add_test(NAME random COMMAND queryparser_random)

  option(QUERYPARSER_FUZZ "Build the libFuzzer target" OFF)
  if(QUERYPARSER_FUZZ)
    add_executable(queryparser_fuzz fuzz/fuzz_parse.cc)
    target_link_libraries(queryparser_fuzz PRIVATE queryparser)
    target_compile_options(queryparser_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(queryparser_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
endif()
