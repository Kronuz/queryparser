// queryparser_t2 parse-v2.jsonl stopwords-dir [EXCEPTIONS-v2.md]: runs every V2 row (T3 runs it over parse-v3.jsonl, same shape) through parse() with
// the settings the row records and prints matched, excepted and failed rows. An exception is a line
// `- ROW | KIND | query: reason` of the markdown file (ROW is the 1-based line of the jsonl); the
// kinds are counted separately. The kind `open` marks a failing row that is tracked: it counts as failed
// but does not fail the run; a new failure, or an open row that now matches, does. A row whose error is the missing-ICU word-breaks error is
// unreachable and excepted by itself.
#include "jsonl.hh"
#include "queryparser.hh"

#include <cctype>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct exception_entry {
	std::string kind;
	std::string why;
};

std::map<std::size_t, exception_entry> load_exceptions(const char* path) {
	std::map<std::size_t, exception_entry> out;
	std::ifstream in(path);
	for (std::string l; std::getline(in, l);) {
		if (l.rfind("- ", 0) != 0 || l.size() < 3 || !std::isdigit(static_cast<unsigned char>(l[2]))) { continue; }
		const auto bar1 = l.find(" | "), bar2 = l.find(" | ", bar1 + 3), colon = l.find(": ", bar2 + 3);
		if (bar1 == std::string::npos || bar2 == std::string::npos || colon == std::string::npos) { continue; }
		out[std::stoul(l.substr(2, bar1 - 2))] = {l.substr(bar1 + 3, bar2 - bar1 - 3), l.substr(colon + 2)};
	}
	return out;
}

analysis::stopper load_stopper(const std::string& file) {
	analysis::stopper s;
	std::ifstream in(file);
	for (std::string w; std::getline(in, w);) {
		if (!w.empty()) { s.add(w); }
	}
	return s;
}

queryparser::expansion expansion_of(const std::map<std::string, std::string>& r, const std::string& name) {
	queryparser::expansion e;
	e.max = std::uint32_t(std::stoul(r.at(name + "_max")));
	const std::string& l = r.at(name + "_limit");
	e.limit = l == "first" ? queryparser::expansion_limit::first
	          : l == "most_frequent" ? queryparser::expansion_limit::most_frequent
	                                 : queryparser::expansion_limit::error;
	return e;
}

// The terms of the strings in a JSON array, decoded.
std::vector<std::string> strings_of(const std::string& raw) {
	std::vector<std::string> out;
	for (std::size_t i = 0; i < raw.size(); ++i) {
		if (raw[i] != '"') { continue; }
		std::size_t j = i + 1;
		while (raw[j] != '"') { j += raw[j] == '\\' ? 2 : 1; }
		out.push_back(jsonl::row("{\"k\":" + raw.substr(i, j - i + 1) + "}")["k"]);
		i = j;
	}
	return out;
}

// The expansion database of strata-oracle: baa00 to baa59 and 31 other words, each bare and under
// title.S, rebuilt from the terms the V2 rows matched.
struct expansion_db {
	std::map<std::string, unsigned> df;  // ordered by term
	void add(const std::string& word, unsigned f) {
		for (const char* prefix : {"", "title.S"}) { df[prefix + word] = f; }
	}
};

bool glob(std::string_view pat, std::string_view s) {
	if (pat.empty()) { return s.empty(); }
	if (pat[0] == '*') { return glob(pat.substr(1), s) || (!s.empty() && glob(pat, s.substr(1))); }
	return !s.empty() && (pat[0] == '?' || pat[0] == s[0]) && glob(pat.substr(1), s.substr(1));
}

unsigned edit_distance(std::string_view a, std::string_view b) {
	std::vector<std::vector<unsigned>> d(a.size() + 1, std::vector<unsigned>(b.size() + 1));
	for (std::size_t i = 0; i <= a.size(); ++i) { d[i][0] = unsigned(i); }
	for (std::size_t j = 0; j <= b.size(); ++j) { d[0][j] = unsigned(j); }
	for (std::size_t i = 1; i <= a.size(); ++i) {
		for (std::size_t j = 1; j <= b.size(); ++j) {
			d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] != b[j - 1])});
			if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) { d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1); }
		}
	}
	return d[a.size()][b.size()];
}

// What the engine would match for the tree's wildcard and edit-distance nodes: the terms, or the
// error message of a cap that is exceeded.
// tie_order is set when the engine's choice among equally frequent terms decides the list.
std::string expand(const queryparser::query& q, const expansion_db& db, std::vector<std::string>& out, bool& tie_order) {
	using queryparser::expansion_limit;
	std::vector<queryparser::node_id> stack;
	if (!q.empty()) { stack.push_back(q.root()); }
	std::vector<queryparser::node_id> order;
	while (!stack.empty()) {
		const auto id = stack.back();
		stack.pop_back();
		order.push_back(id);
		std::vector<queryparser::node_id> kids;
		for (auto c = q.at(id).first; c != queryparser::no_node; c = q.at(c).next) { kids.push_back(c); }
		stack.insert(stack.end(), kids.rbegin(), kids.rend());
	}
	for (auto id : order) {
		const auto& n = q.at(id);
		const std::string text(q.text(n));
		if (n.k == queryparser::kind::term) {
			if ((order.size() == 1 || q.at(q.root()).solid) && db.df.count(text) && std::find(out.begin(), out.end(), text) == out.end()) { out.push_back(text); }
			continue;
		}
		if (n.k != queryparser::kind::wildcard && n.k != queryparser::kind::edit_distance) { continue; }
		std::vector<std::pair<std::string, unsigned>> hit;
		for (const auto& [term, f] : db.df) {
			if (n.k == queryparser::kind::wildcard ? (text.find_first_of("*?") != std::string::npos ? glob(text, term) : term.compare(0, text.size(), text) == 0)
			                                       : term.compare(0, n.pos, text, 0, n.pos) == 0 && edit_distance(term.substr(n.pos), text.substr(n.pos)) <= n.window) {
				hit.emplace_back(term, f);
			}
		}
		if (n.max_expansion != 0 && hit.size() > n.max_expansion && n.limit == expansion_limit::error) {
			const std::string what = n.k == queryparser::kind::edit_distance ? "Edit distance " + text + "~" + std::to_string(n.window)
			                                                                 : "Wildcard " + text + (text.find_first_of("*?") == std::string::npos ? "*" : "");
			return what + " expands to more than " + std::to_string(n.max_expansion) + " terms";
		}
		if (n.limit == expansion_limit::most_frequent && (n.max_expansion == 0 || hit.size() > n.max_expansion)) {
			if (n.max_expansion != 0) { tie_order = true; }
			std::stable_sort(hit.begin(), hit.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			hit.resize(std::min<std::size_t>(hit.size(), n.max_expansion));
			std::sort(hit.begin(), hit.end());
		} else if (n.limit == expansion_limit::first && n.max_expansion != 0 && hit.size() > n.max_expansion) {
			hit.resize(n.max_expansion);
		}
		for (auto& h : hit) {
			if (std::find(out.begin(), out.end(), h.first) == out.end()) { out.push_back(h.first); }
		}
	}
	return {};
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 3) { return 2; }
	const std::string stopdir = argv[2];
	const auto exceptions = argc > 3 ? load_exceptions(argv[3]) : std::map<std::size_t, exception_entry>{};
	std::map<std::string, analysis::stopper> stoppers;
	for (const char* lang : {"en", "es", "fr"}) { stoppers[lang] = load_stopper(stopdir + "/" + lang + ".txt"); }
	std::map<std::string, std::unique_ptr<snowball::stemmer>> stemmers;
	for (const char* lang : {"english", "spanish", "french"}) { stemmers[lang] = std::make_unique<snowball::stemmer>(lang); }
	const std::map<std::string, std::uint32_t> flag_names = {
	    {"boolean", queryparser::flag::boolean},     {"phrase", queryparser::flag::phrase},
	    {"lovehate", queryparser::flag::lovehate},   {"wildcard", queryparser::flag::wildcard},
	    {"partial", queryparser::flag::partial},     {"fuzzy", queryparser::flag::fuzzy},
	    {"cjk_ngram", queryparser::flag::cjk_ngram}, {"wildcard_single", queryparser::flag::wildcard_single},
	    {"wildcard_multi", queryparser::flag::wildcard_multi}, {"word_breaks", 0},
	    {"pure_not", queryparser::flag::pure_not}, {"boolean_any_case", queryparser::flag::boolean_any_case}, {"-", 0}};
	const std::map<std::string, analysis::stem_strategy> stems = {{"unset", analysis::stem_strategy::none}, {"none", analysis::stem_strategy::none},
	                                                              {"some", analysis::stem_strategy::some}, {"all", analysis::stem_strategy::all},
	                                                              {"all_z", analysis::stem_strategy::all_z}};
	const std::map<std::string, queryparser::kind> ops = {
	    {"unset", queryparser::kind::or_},   {"or", queryparser::kind::or_},           {"and", queryparser::kind::and_},
	    {"near", queryparser::kind::near},   {"phrase", queryparser::kind::phrase},    {"elite_set", queryparser::kind::elite_set},
	    {"synonym", queryparser::kind::synonym}, {"max", queryparser::kind::max}};
	const std::map<std::string, analysis::stop_strategy> stops = {{"default", analysis::stop_strategy::stemmed}, {"stemmed", analysis::stop_strategy::stemmed},
	                                                              {"all", analysis::stop_strategy::all}, {"none", analysis::stop_strategy::none}};
	expansion_db db;
	{
		std::set<std::string> words;
		std::ifstream pre(argv[1]);
		for (std::string line; std::getline(pre, line);) {
			const auto r = jsonl::row(line);
			if (r.at("group") != "expand" || r.at("expanded")[0] != '[') { continue; }
			for (const auto& t : strings_of(r.at("expanded"))) {
				if (t.rfind("title.S", 0) != 0 && t.rfind("S", 0) != 0) { words.insert(t); }
			}
		}
		for (const auto& w : words) {
			unsigned f = w == "obamas" ? 4 : 2;
			if (w.size() == 5 && w.rfind("baa", 0) == 0) { f = 1 + unsigned((std::stoul(w.substr(3)) * 37) % 11); }
			db.add(w, f);
		}
	}
	std::size_t matched = 0, failed = 0, unused = 0, n = 0, tie_rows = 0, open_rows = 0;
	std::map<std::string, std::size_t> by_kind, failed_by_group, by_group;
	std::set<std::size_t> used;
	std::ifstream in(argv[1]);
	for (std::string line; std::getline(in, line);) {
		++n;
		const auto r = jsonl::row(line);
		queryparser::config cfg;
		cfg.flags = 0;
		std::istringstream fs(r.at("flags"));
		for (std::string f; std::getline(fs, f, '|');) { cfg.flags |= flag_names.at(f); }
		cfg.default_op = ops.at(r.at("default_op"));
		if (r.at("stopper") != "none") { cfg.stopper = &stoppers.at(r.at("stopper")); }
		cfg.stop = stops.at(r.at("stop_strategy"));
		cfg.stem = stems.at(r.at("stem"));
		if (r.at("stem") != "unset") { cfg.stemmer = stemmers.at(r.at("stem_lang")).get(); }
		cfg.default_prefix = r.at("prefix");
		cfg.max_word_length = std::size_t(1) << 20;
		cfg.partial_expansion = expansion_of(r, "partial");
		cfg.wildcard_expansion = expansion_of(r, "wildcard");
		cfg.fuzzy_expansion = expansion_of(r, "fuzzy");
		const auto res = queryparser::parse(r.at("query"), cfg);
		std::string got = res.err ? "ERROR " + res.err->message : res.tree.describe();
		std::string want = r.at("error").empty() ? r.at("description") : "ERROR " + r.at("error");
		// A row with an expansion outcome also pins what the node caps do against the database.
		if (const auto it = r.find("expanded"); it != r.end() && !res.err) {
			std::vector<std::string> terms;
			bool tie_order = false;
			const std::string err = expand(res.tree, db, terms, tie_order);
			std::string exp;
			if (it->second[0] == '{') {
				exp = "ERROR " + jsonl::row("{\"k\":" + it->second.substr(it->second.find("\"error\":", 20) + 8) )["k"];
			} else {
				for (const auto& s : strings_of(it->second)) { exp += s + ","; }
			}
			std::string have = err.empty() ? "" : "ERROR " + err;
			if (err.empty()) { for (const auto& s : terms) { have += s + ","; } }
			if (tie_order) {
				++tie_rows;  // which of the equally frequent terms stay is the engine's choice: not compared
			} else {
				got += " => " + have;
				want += " => " + exp;
			}
		}
		const std::string& group = r.at("group");
		++by_group[group];
		if (got == want) {
			++matched;
			continue;
		}
		if (r.at("error") == "FLAG_WORD_BREAKS requires building Xapian to use ICU") {
			++by_kind["unreachable"];
			continue;
		}
		if (auto ex = exceptions.find(n); ex != exceptions.end()) {
			used.insert(n);
			// An open row is a failure that is tracked: it is listed, counted as failed, and does not fail the run.
			if (ex->second.kind == "open") {
				++open_rows;
				++failed_by_group[group];
				std::printf("open row %zu [%s]: %s\n  got:  %s\n  want: %s\n", n, group.c_str(), r.at("query").c_str(), got.c_str(), want.c_str());
				continue;
			}
			++by_kind[ex->second.kind];
			std::printf("excepted [%s] row %zu: %s\n  got:  %s\n  want: %s\n  why:  %s\n", ex->second.kind.c_str(), n, r.at("query").c_str(),
			            got.c_str(), want.c_str(), ex->second.why.c_str());
			continue;
		}
		++failed;
		++failed_by_group[group];
		std::printf("FAILED row %zu [%s] %s | %s | op=%s stop=%s/%s stem=%s/%s prefix=%s\n  got:  %s\n  want: %s\n", n, group.c_str(),
		            r.at("query").c_str(), r.at("flags").c_str(), r.at("default_op").c_str(), r.at("stopper").c_str(),
		            r.at("stop_strategy").c_str(), r.at("stem_lang").c_str(), r.at("stem").c_str(), r.at("prefix").c_str(), got.c_str(),
		            want.c_str());
	}
	for (const auto& [row, e] : exceptions) {
		if (!used.count(row)) { ++unused; std::printf("unused exception: row %zu\n", row); }
	}
	for (const auto& [g, f] : failed_by_group) { std::printf("failed in %s: %zu of %zu\n", g.c_str(), f, by_group[g]); }
	std::size_t excepted = 0;
	for (const auto& [k, c] : by_kind) { excepted += c; std::printf("excepted (%s): %zu\n", k.c_str(), c); }
	std::printf("rows whose most_frequent cut ties among equally frequent terms (list not compared): %zu\n", tie_rows);
	std::printf("rows %zu: matched %zu, excepted %zu, failed %zu (%zu of them open and tracked, %zu new)\n", n, matched, excepted, failed + open_rows,
	            open_rows, failed);
	return failed || unused ? 1 : 0;
}
