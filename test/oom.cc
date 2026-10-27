// An allocation that fails at any point of a parse becomes error_kind::out_of_memory: parse
// never throws, and (under ASan) nothing leaks. Every allocation of each query is failed in turn.
#include "queryparser.hh"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#if defined(__SANITIZE_THREAD__)
#define QP_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define QP_TSAN 1
#endif
#endif

#ifndef QP_TSAN
static std::size_t allocations = 0;
static std::size_t fail_at = 0;  // the allocation to fail, 1-based; 0: none

void* operator new(std::size_t n) {
	++allocations;
	if (allocations == fail_at) { throw std::bad_alloc(); }
	if (void* p = std::malloc(n ? n : 1)) { return p; }
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#endif

int main() {
#ifdef QP_TSAN
	std::puts("oom: skipped under TSan, whose runtime owns operator new");
	return 77;
#else
	static snowball::stemmer english("english");
	using namespace queryparser;
	config cfg;
	cfg.flags = default_flags | flag::wildcard | flag::partial | flag::fuzzy | flag::cjk_ngram | flag::pure_not | flag::boolean_any_case;
	cfg.stem = analysis::stem_strategy::some;
	cfg.stemmer = &english;
	cfg.resolver = [](std::string_view name, field_spec& out) {
		if (name != "title") { return false; }
		out.prefix = "a-long-field-prefix-past-the-small-string-size";
		return true;
	};
	const char* const queries[] = {
		"+barack -\"michelle obama\" title:44th NEAR/3 president",
		"(a OR b) AND NOT c running* foo~2 中文分词 x-y-z ba",
		"a AND -b ((c d) e",
		"A.B.C. u.s.a title:(a b) a NEAR b* ADJ c",
		"NOT a AND",
	};
	int failures = 0;
	std::size_t points = 0;
	for (const char* q : queries) {
		allocations = 0;
		fail_at = 0;
		(void)parse(q, cfg);
		const std::size_t total = allocations;
		for (std::size_t n = 1; n <= total; ++n) {
			allocations = 0;
			fail_at = n;
			try {
				const auto r = parse(q, cfg);
				if (!r.err || r.err->kind != error_kind::out_of_memory) {
					std::fprintf(stderr, "%s: allocation %zu of %zu: no out_of_memory error\n", q, n, total);
					++failures;
				}
			} catch (...) {
				std::fprintf(stderr, "%s: allocation %zu of %zu: parse threw\n", q, n, total);
				++failures;
			}
			++points;
		}
	}
	fail_at = 0;
	std::printf("queryparser oom: %zu failure points: %s\n", points, failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
#endif
}
