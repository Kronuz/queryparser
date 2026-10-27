// libFuzzer target: the first byte selects flags, stemming and the default operator.
#include "queryparser.hh"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	static snowball::stemmer english("english");
	if (size == 0) { return 0; }
	queryparser::config cfg;
	cfg.flags = data[0] & 7u;
	cfg.stem = analysis::stem_strategy((data[0] >> 3) & 3u);
	cfg.default_op = data[0] & 0x20 ? queryparser::kind::and_ : queryparser::kind::or_;
	cfg.stemmer = data[0] & 0x40 ? nullptr : &english;
	const auto r = queryparser::parse(std::string_view(reinterpret_cast<const char*>(data) + 1, size - 1), cfg);
	if (!r.err) { (void)r.tree.describe(); }
	return 0;
}
