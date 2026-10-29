// libFuzzer target: the first three bytes select flags, stemming, the default operator, a
// field resolver and a term-text cap; the rest is the query. The tree must be consistent.
#include "queryparser.hh"
#include "../test/tree_check.hh"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	static snowball::stemmer english("english");
	if (size < 4) { return 0; }
	static const analysis::stopper stop{"a", "the", "and", "or", "near", "b"};
	queryparser::config cfg;
	cfg.flags = data[0] | (std::uint32_t(data[1] & 1) << 8) | (std::uint32_t(data[3] & 3) << 9);
	if (data[3] & 4) { cfg.stopper = &stop; }
	cfg.stop = analysis::stop_strategy(((data[3] >> 3) & 3) % 3);
	if (data[3] & 0x20) { cfg.default_prefix = data[3] & 0x40 ? "title.S" : "S"; }
	cfg.fuzzy_expansion = {std::uint32_t(data[3] >> 7), queryparser::expansion_limit(data[2] % 3)};
	cfg.stem = analysis::stem_strategy(data[2] & 3u);
	static const queryparser::kind ops[] = {queryparser::kind::or_,     queryparser::kind::and_,    queryparser::kind::xor_,
	                                        queryparser::kind::and_maybe, queryparser::kind::near,    queryparser::kind::phrase,
	                                        queryparser::kind::elite_set, queryparser::kind::synonym, queryparser::kind::max,
	                                        queryparser::kind::term};
	cfg.default_op = ops[(data[2] >> 4) % 10];
	cfg.stemmer = data[2] & 0x08 ? nullptr : &english;
	if (data[1] & 2) {
		cfg.resolver = [](std::string_view name, queryparser::field_spec& out) {
			if (name.size() % 3 == 0) { return false; }
			out.prefix.assign(name.size() * 7, 'P');
			return true;
		};
	}
	if (data[1] & 4) { cfg.max_term_bytes = 200 + (data[1] >> 3); }
	if (data[2] & 0x04) { cfg.max_word_length = data[1] & 0x80 ? 0 : 5; }
	const std::string_view text(reinterpret_cast<const char*>(data) + 4, size - 4);
	const auto r = queryparser::parse(text, cfg);
	if (r.err) {
		if (r.err->offset > text.size()) { std::abort(); }
		return 0;
	}
	(void)r.tree.describe();
	// Every node is reached once from the root (no cycle, one parent), links stay inside the tree,
	// and every node's text lies in the pool; walk without recursion.
	if (!queryparser::well_formed(r.tree)) { std::abort(); }
	for (std::size_t id = 0; id < r.tree.size(); ++id) {
		const auto& n = r.tree.at(queryparser::node_id(id));
		if (r.tree.text(n).size() != n.text_len) { std::abort(); }
	}
	return 0;
}
