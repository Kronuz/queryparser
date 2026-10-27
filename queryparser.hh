/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// A query-string parser: text such as `+barack -"michelle obama" title:44th NEAR/3 president`
// becomes a tree of terms and operators. The parser is iterative: nesting depth is bounded
// by memory, never by the call stack.

#pragma once

#include "analysis.hh"
#include "snowball.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace queryparser {

// Syntax switches, combined with `|`.
namespace flag {
inline constexpr std::uint32_t boolean = 1u << 0;   // AND OR NOT XOR NEAR ADJ and brackets
inline constexpr std::uint32_t phrase = 1u << 1;    // "quoted phrases" and phrase generators (a-b)
inline constexpr std::uint32_t lovehate = 1u << 2;  // +required and -excluded
}  // namespace flag

inline constexpr std::uint32_t default_flags = flag::boolean | flag::phrase | flag::lovehate;

enum class kind : std::uint8_t {
	term,
	and_,
	or_,
	and_not,
	xor_,
	and_maybe,
	near,
	phrase,
	synonym,
	elite_set,
	max,
};

using node_id = std::uint32_t;
inline constexpr node_id no_node = ~node_id(0);

// A node of the tree. Children are a list: first child, then each child's `next`.
struct node {
	kind k = kind::term;
	std::uint32_t pos = 0;     // term: position
	std::uint32_t wqf = 1;     // term: query frequency
	std::uint32_t window = 0;  // near, phrase: window in positions
	std::uint32_t text_off = 0;
	std::uint32_t text_len = 0;
	node_id first = no_node;
	node_id last = no_node;
	node_id next = no_node;
};

// A parsed query. An empty query has no root and matches nothing.
class query {
public:
	node_id root() const noexcept { return root_; }
	bool empty() const noexcept { return root_ == no_node; }
	const node& at(node_id id) const noexcept { return nodes_[id]; }
	std::size_t size() const noexcept { return nodes_.size(); }
	std::string_view text(const node& n) const noexcept { return std::string_view(pool_).substr(n.text_off, n.text_len); }

	// The tree in the format of Xapian's Query::get_description(): `Query((a@1 OR b@2))`.
	std::string describe() const;

private:
	friend class builder;
	std::vector<node> nodes_;
	std::string pool_;
	node_id root_ = no_node;
};

enum class error_kind : std::uint8_t {
	syntax,    // an operator lacks an operand
	too_long,  // the text is over max_query_bytes
};

struct error {
	error_kind kind = error_kind::syntax;
	std::size_t offset = 0;  // byte offset in the text
	std::string message;
};

// How a field name in `name:` resolves: the prefix its terms carry. Return false for a name
// that is not a field, which leaves `name:x` as the phrase `name x`.
struct field_spec {
	std::string prefix;
};
using field_resolver = std::function<bool(std::string_view name, field_spec& out)>;

// Query text over this size is rejected: node positions and offsets are 32 bits.
inline constexpr std::size_t max_query_bytes = std::size_t(1) << 30;

struct config {
	std::uint32_t flags = default_flags;
	analysis::stem_strategy stem = analysis::stem_strategy::some;
	kind default_op = kind::or_;  // or_, and_, xor_ or and_maybe
	snowball::stemmer* stemmer = nullptr;  // none: words are not stemmed
	std::size_t max_word_length = 64;      // bytes after lowercasing; longer words are dropped
	field_resolver resolver;               // none: `name:x` is the phrase `name x`
};

struct parse_result {
	query tree;
	std::optional<error> err;
	explicit operator bool() const noexcept { return !err; }
};

// Parses text; never throws on input.
parse_result parse(std::string_view text, const config& cfg = {});

}  // namespace queryparser
