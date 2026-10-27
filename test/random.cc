// Random input: no crash and bounded memory on 1 MB of bytes, with and without syntax bias.
#include "queryparser.hh"

#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

int main() {
	snowball::stemmer english("english");
	std::mt19937_64 rng(12345);
	const std::string syntax = " ()\"+-:./@\\ANDORXNEAJTab12";
	const char* words[] = {" AND ", " OR ", " NOT ", " XOR ", " NEAR/3 ", " ADJ ", "(", ")", "\"", "-", "+", "a", "bb", "foo", "U.S.A.", "x-y", "foo*", "ab~", "~2", "NEAR ", "\xe4\xb8\xad\xe6\x96\x87"};
	std::size_t checked = 0;
	for (int round = 0; round < 6; ++round) {
		std::string s;
		const std::size_t size = 1 << 20;
		while (s.size() < size) {
			switch (round % 3) {
				case 0: s += char(rng()); break;
				case 1: s += syntax[rng() % syntax.size()]; break;
				default: s += words[rng() % (sizeof words / sizeof *words)]; break;
			}
		}
		s.resize(size);
		for (std::uint32_t flags : {queryparser::default_flags, 0u, queryparser::flag::boolean, 0x1FFu, 0x1FFu & ~queryparser::flag::boolean}) {
			queryparser::config cfg;
			cfg.flags = flags;
			cfg.stemmer = &english;
			cfg.stem = analysis::stem_strategy(round % 4);
			cfg.default_op = round & 1 ? queryparser::kind::and_ : queryparser::kind::or_;
			auto r = queryparser::parse(s, cfg);
			checked += r.err ? 1 : r.tree.describe().size() > 0;
		}
	}
	rusage ru;
	getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
	const double mib = double(ru.ru_maxrss) / (1024 * 1024);
#else
	const double mib = double(ru.ru_maxrss) / 1024;
#endif
	std::printf("queryparser random: %zu inputs of 1 MiB, peak RSS %.0f MiB: %s\n", checked, mib, mib < 1024 ? "ok" : "FAILED");
	return mib < 1024 ? EXIT_SUCCESS : EXIT_FAILURE;
}
