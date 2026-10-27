// Deep nesting needs no recursion: runs on a 512 KiB thread stack.
#include "queryparser.hh"

#include <pthread.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); ++failures; } \
	} while (0)

constexpr std::size_t N = 1000000;

// Parses, describes and destroys one input.
void run(const std::string& text, const char* what, std::size_t min_describe) {
	queryparser::config cfg;
	cfg.stem = analysis::stem_strategy::none;
	auto r = queryparser::parse(text, cfg);
	CHECK(!r.err);
	const std::string d = r.tree.describe();
	CHECK(d.size() >= min_describe);
	std::printf("%s: %zu nodes, %zu bytes described\n", what, r.tree.size(), d.size());
}

void* body(void*) {
	std::string s;
	// Alternating operators keep every bracket level a separate node.
	for (std::size_t i = 0; i < N; ++i) { s += i % 2 ? "(a OR " : "(a AND "; }
	s += "a";
	s += std::string(N, ')');
	run(s, "nested brackets", 6 * N);
	// Unclosed brackets close at the end.
	s.assign(N, '(');
	s += "a b";
	run(s, "unclosed brackets", 5);
	// A left-deep chain of NOT.
	s.clear();
	for (std::size_t i = 0; i < N; ++i) { s += "a NOT "; }
	s += "a";
	run(s, "NOT chain", 6 * N);
	// A right-deep chain of NOT.
	s.clear();
	for (std::size_t i = 0; i < N; ++i) { s += "a NOT ("; }
	s += "a";
	s += std::string(N, ')');
	run(s, "right NOT chain", 6 * N);
	// Long flat chains and a stray run of closing brackets.
	s.clear();
	for (std::size_t i = 0; i < N; ++i) { s += "a OR "; }
	s += "a";
	run(s, "OR chain", 5 * N);
	run(std::string(N, ')'), "closing brackets", 0);
	run(std::string(N, '"'), "quotes", 0);
	return nullptr;
}

}  // namespace

int main() {
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 512 * 1024);
	pthread_t t;
	if (pthread_create(&t, &attr, body, nullptr) != 0) { return 2; }
	pthread_join(t, nullptr);
	std::printf("queryparser deep: %s\n", failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
