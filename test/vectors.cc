// queryparser_vectors parse.jsonl [EXCEPTIONS.md]: runs the oracle rows (query, flags,
// stem, description, error) through parse() and prints matched, excepted, failed and
// skipped rows. A row is skipped when it needs a flag or setting not implemented yet.
#include "jsonl.hh"
#include "queryparser.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace {

}  // namespace

int main(int argc, char** argv) {
	if (argc < 2) { return 2; }
	// Exceptions: lines `- query | flags | stem: reason` of the markdown file.
	std::map<std::string, std::string> exceptions;
	if (argc > 2) {
		std::ifstream ex(argv[2]);
		for (std::string l; std::getline(ex, l);) {
			if (l.rfind("- ", 0) != 0) { continue; }
			const auto colon = l.find(": ");
			if (colon != std::string::npos) { exceptions[l.substr(2, colon - 2)] = l.substr(colon + 2); }
		}
	}
	snowball::stemmer english("english");
	const std::map<std::string, std::uint32_t> names = {{"default", queryparser::default_flags}, {"boolean", queryparser::flag::boolean},
	                                                    {"phrase", queryparser::flag::phrase}, {"lovehate", queryparser::flag::lovehate},
	                                                    {"wildcard", queryparser::flag::wildcard}, {"partial", queryparser::flag::partial},
	                                                    {"fuzzy", queryparser::flag::fuzzy}, {"cjk_ngram", queryparser::flag::cjk_ngram},
	                                                    {"pure_not", queryparser::flag::pure_not},
	                                                    {"boolean_any_case", queryparser::flag::boolean_any_case}};
	const std::map<std::string, analysis::stem_strategy> stems = {{"none", analysis::stem_strategy::none}, {"some", analysis::stem_strategy::some},
	                                                              {"all", analysis::stem_strategy::all}, {"all_z", analysis::stem_strategy::all_z}};
	std::size_t matched = 0, excepted = 0, failed = 0, skipped = 0, unused = 0;
	std::set<std::string> used;
	std::map<std::string, std::size_t> skipped_flags;
	std::ifstream in(argv[1]);
	for (std::string line; std::getline(in, line);) {
		auto r = jsonl::row(line);
		queryparser::config cfg;
		cfg.flags = 0;
		bool supported = true;
		std::istringstream fs(r["flags"]);
		for (std::string f; std::getline(fs, f, '|');) {
			auto it = names.find(f);
			if (it == names.end()) {
				supported = false;
			} else {
				cfg.flags |= it->second;
			}
		}
		if (!supported) {
			++skipped;
			++skipped_flags[r["flags"]];
			continue;
		}
		cfg.stem = stems.at(r["stem"]);
		cfg.stemmer = &english;
		const auto res = queryparser::parse(r["query"], cfg);
		const std::string got = res.err ? "ERROR " + res.err->message : res.tree.describe();
		const std::string want = r["error"].empty() ? r["description"] : "ERROR " + r["error"];
		const std::string key = r["query"] + " | " + r["flags"] + " | " + r["stem"];
		if (got == want) {
			++matched;
		} else if (auto ex = exceptions.find(key); ex != exceptions.end()) {
			++excepted;
			used.insert(key);
			std::printf("excepted: %s\n  got:  %s\n  want: %s\n  why:  %s\n", key.c_str(), got.c_str(), want.c_str(), ex->second.c_str());
		} else {
			++failed;
			std::printf("FAILED: %s\n  got:  %s\n  want: %s\n", key.c_str(), got.c_str(), want.c_str());
		}
	}
	for (const auto& [k, v] : exceptions) {
		if (!used.count(k)) { ++unused; std::printf("unused exception: %s\n", k.c_str()); }
	}
	for (const auto& [f, n] : skipped_flags) { std::printf("skipped %zu rows: flags %s\n", n, f.c_str()); }
	std::printf("matched %zu, excepted %zu, failed %zu, skipped %zu\n", matched, excepted, failed, skipped);
	return failed || unused ? 1 : 0;
}
