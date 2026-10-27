// queryparser_vectors parse.jsonl [EXCEPTIONS.md]: runs the oracle rows (query, flags,
// stem, description, error) through parse() and prints matched, excepted, failed and
// skipped rows. A row is skipped when it needs a flag or setting not implemented yet.
#include "queryparser.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace {

// One flat JSON object of string values; strings keep their decoded UTF-8.
std::map<std::string, std::string> row(const std::string& line) {
	std::map<std::string, std::string> m;
	const char* p = line.c_str();
	auto str = [&] {
		std::string s;
		for (++p; *p != '"'; ++p) {
			if (*p != '\\') {
				s += *p;
				continue;
			}
			switch (*++p) {
				case 'n': s += '\n'; break;
				case 't': s += '\t'; break;
				case 'r': s += '\r'; break;
				case 'u': {
					char32_t c = char32_t(std::strtoul(std::string(p + 1, 4).c_str(), nullptr, 16));
					p += 4;
					if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
						const char32_t lo = char32_t(std::strtoul(std::string(p + 3, 4).c_str(), nullptr, 16));
						c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
						p += 6;
					}
					unicode::append_utf8(s, c);
					break;
				}
				default: s += *p;
			}
		}
		++p;
		return s;
	};
	while (*p && *p != '}') {
		if (*p == '"') {
			std::string k = str();
			++p;  // ':'
			m[k] = str();
		} else {
			++p;
		}
	}
	return m;
}

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
	                                                    {"phrase", queryparser::flag::phrase}, {"lovehate", queryparser::flag::lovehate}};
	const std::map<std::string, analysis::stem_strategy> stems = {{"none", analysis::stem_strategy::none}, {"some", analysis::stem_strategy::some},
	                                                              {"all", analysis::stem_strategy::all}, {"all_z", analysis::stem_strategy::all_z}};
	std::size_t matched = 0, excepted = 0, failed = 0, skipped = 0, unused = 0;
	std::set<std::string> used;
	std::map<std::string, std::size_t> skipped_flags;
	std::ifstream in(argv[1]);
	for (std::string line; std::getline(in, line);) {
		auto r = row(line);
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
