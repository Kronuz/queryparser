// libFuzzer target: the first three bytes select flags, stemming and the default operator.
#include "queryparser.hh"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	static snowball::stemmer english("english");
	if (size < 3) { return 0; }
	queryparser::config cfg;
	cfg.flags = data[0] | (std::uint32_t(data[1] & 1) << 8);
	cfg.stem = analysis::stem_strategy(data[2] & 3u);
	cfg.default_op = data[2] & 0x04 ? queryparser::kind::and_ : queryparser::kind::or_;
	cfg.stemmer = data[2] & 0x08 ? nullptr : &english;
	const auto r = queryparser::parse(std::string_view(reinterpret_cast<const char*>(data) + 3, size - 3), cfg);
	if (!r.err) { (void)r.tree.describe(); }
	return 0;
}
