// libFuzzer target: the first three bytes select flags, stemming, the default operator, a
// field resolver and a term-text cap; the rest is the query. The tree must be consistent.
#include "queryparser.hh"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	static snowball::stemmer english("english");
	if (size < 3) { return 0; }
	queryparser::config cfg;
	cfg.flags = data[0] | (std::uint32_t(data[1] & 1) << 8);
	cfg.stem = analysis::stem_strategy(data[2] & 3u);
	static const queryparser::kind ops[] = {queryparser::kind::or_, queryparser::kind::and_, queryparser::kind::xor_,
	                                        queryparser::kind::and_maybe, queryparser::kind::near, queryparser::kind::term};
	cfg.default_op = ops[(data[2] >> 4) % 6];
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
	const std::string_view text(reinterpret_cast<const char*>(data) + 3, size - 3);
	const auto r = queryparser::parse(text, cfg);
	if (r.err) {
		if (r.err->offset > text.size()) { std::abort(); }
		return 0;
	}
	(void)r.tree.describe();
	// Every node's text lies in the pool and every child index is a node; walk without recursion.
	std::vector<queryparser::node_id> stack;
	if (!r.tree.empty()) { stack.push_back(r.tree.root()); }
	std::size_t visited = 0;
	while (!stack.empty()) {
		const auto id = stack.back();
		stack.pop_back();
		if (id >= r.tree.size() || ++visited > r.tree.size()) { std::abort(); }
		const auto& n = r.tree.at(id);
		if (r.tree.text(n).size() != n.text_len) { std::abort(); }
		for (auto c = n.first; c != queryparser::no_node; c = r.tree.at(c).next) { stack.push_back(c); }
	}
	return 0;
}
