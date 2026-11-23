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

// The parser's internals: the node builder, the items, frames and pending operators of a group,
// and the iterative parser's class, whose members are defined by layer in parser_*.hh. Included
// only by queryparser.cc.

#pragma once

#include "queryparser.hh"

#include "unicode.hh"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Checked builds (the tests, the fuzz target, any build without NDEBUG) verify the tree as it is built.
#ifndef QUERYPARSER_CHECKS
#ifdef NDEBUG
#define QUERYPARSER_CHECKS 0
#else
#define QUERYPARSER_CHECKS 1
#endif
#endif

namespace queryparser {

namespace {
// Thrown when the term text or the node count would not fit the 32-bit links.
struct limit_exceeded {};
}  // namespace

// Builds a query's nodes.
class builder {
public:
	builder(query& q, std::size_t max_pool) : q_(q), max_pool_(max_pool < max_term_bytes_limit ? max_pool : max_term_bytes_limit) {}

	node_id term(std::string_view text, std::uint32_t pos) {
		node n;
		n.pos = pos;
		store(n, text);
		return push(n);
	}

	node_id leaf(kind k, std::string_view text, std::uint32_t window = 0, expansion ex = {}) {
		node n;
		n.k = k;
		n.window = window;
		n.max_expansion = ex.max;
		n.limit = ex.limit;
		store(n, text);
		return push(n);
	}

	// A new leaf that reads as leaf does, so two parents never share one node.
	node_id copy_leaf(node_id leaf) {
		node n = q_.nodes_[leaf];
		n.next = no_node;
		return push(n);
	}

	void set_pos(node_id id, std::uint32_t pos) { q_.nodes_[id].pos = pos; }

	node_id match_all_scaled(double factor) {
		node m;
		m.k = kind::match_all;
		const node_id c = push(m);
		node s;
		s.k = kind::scale;
		s.factor = factor;
		const node_id id = push(s);
		add(id, c);
		return id;
	}

	node_id op(kind k, std::uint32_t window = 0, bool solid = false) {
		node n;
		n.k = k;
		n.window = window;
		n.solid = solid;
		return push(n);
	}

	// Appends c to the children of parent; c's own `next` is cleared. A node has one parent and
	// appears once, or a child list could link back on itself: checked builds abort on a repeat.
	void add(node_id parent, node_id c) {
#if QUERYPARSER_CHECKS
		if (c == parent || attached_[c]) { std::abort(); }
		attached_[c] = true;
#endif
		node& p = q_.nodes_[parent];
		q_.nodes_[c].next = no_node;
		if (p.last == no_node) {
			p.first = c;
		} else {
			q_.nodes_[p.last].next = c;
		}
		q_.nodes_[parent].last = c;
	}

	// l OP r, where an AND, OR or XOR chain extends l, the left operand, into one n-ary node.
	node_id combine(kind k, node_id l, node_id r) {
		const bool flat = k == kind::and_ || k == kind::or_ || k == kind::xor_;
		node_id res;
		if (flat && q_.nodes_[l].k == k && !q_.nodes_[l].solid) {
			res = l;
		} else {
			res = op(k);
			add(res, l);
		}
		add(res, r);
		return res;
	}

	const node& at(node_id id) const { return q_.nodes_[id]; }
	void set_root(node_id id) { q_.root_ = id; }
	void reserve(std::size_t n) { q_.nodes_.reserve(n); }

private:
	// Appends text to the pool; the pool never exceeds max_pool_ bytes, so offsets cannot wrap.
	void store(node& n, std::string_view text) {
		if (text.size() > max_pool_ - q_.pool_.size()) { throw limit_exceeded{}; }
		n.text_off = std::uint32_t(q_.pool_.size());
		n.text_len = std::uint32_t(text.size());
		q_.pool_.append(text);
	}
	node_id push(const node& n) {
		if (q_.nodes_.size() >= no_node) { throw limit_exceeded{}; }
		q_.nodes_.push_back(n);
#if QUERYPARSER_CHECKS
		attached_.push_back(false);
#endif
		return node_id(q_.nodes_.size() - 1);
	}
#if QUERYPARSER_CHECKS
	std::vector<bool> attached_;  // the nodes some add() has made a child
#endif
	query& q_;
	std::size_t max_pool_;
};

namespace {

enum class mark : std::uint8_t { none, plus, minus };

// Internal flag of a re-read: a double quote is punctuation.
constexpr std::uint32_t plain_quotes = 1u << 31;

// A thing in a group of juxtaposed items. n == no_node is a dropped stop word, which still
// counts as an operand.
struct item {
	node_id n;
	mark m;
	std::size_t offset;
	std::size_t end;
	bool plain;  // a plain word: adjacent plain words form a run
	bool stop;   // a plain word that is a stop word
	bool anchor = false;  // a wildcard or an edit distance: stop words beside it go
	bool solo = false;    // a word that stands alone: a run of its own
	bool kw = false;      // an upper-case operator word
	bool quote = false;   // a quoted phrase
	bool group = false;   // a bracket
	node_id alt = no_node;  // a partial pair: its term alone, which an exclusion that reads as unmarked uses
	bool glued = false;     // a word joined by a generator to the wildcard or edit distance after it: a run of its own
};

struct pending_op {
	kind k;
	std::size_t offset;
	const char* label;  // the operator as the user wrote it
	bool split;         // an implicit operator between the parts a stray `)` separates
	std::size_t end = 0;  // where the operator word ends
};

struct frame {
	std::size_t ops_base;
	std::size_t vals_base;
	std::size_t items_base;
	bool parent_expect;
	mark m;
	std::size_t offset;
};

struct raw_word {
	std::string text;  // lowercased
	bool capital;
	bool same_pos = false;  // a CJK unit: shares the position of the unit before it
	bool star = false;  // followed by `*`
};

constexpr int prec(kind k) noexcept {
	switch (k) {
		case kind::or_: return 1;
		case kind::xor_: return 2;
		default: return 3;  // and_, and_not, and the implicit separator
	}
}

constexpr bool is_generator(char c) noexcept { return c == '-' || c == ':' || c == '/' || c == '.' || c == '@' || c == '\\'; }
constexpr bool is_space(char c) noexcept { return c == ' ' || (c >= '\t' && c <= '\r'); }

class parser {
public:
	parser(std::string_view text, const config& cfg, parse_result& out);

	bool needs_reparse() const { return reparse_; }

	void run();

private:
	bool boolean() const { return cfg_.flags & flag::boolean; }
	bool phrase() const { return cfg_.flags & flag::phrase; }
	bool lovehate() const { return cfg_.flags & flag::lovehate; }
	bool has(std::uint32_t f) const { return cfg_.flags & f; }
	const frame& top() const { return frames_.back(); }

	std::size_t off(const char* p) const { return std::size_t(p - b_); }
	bool word_at(const char* p) const;

	void fail(std::size_t offset, std::string message, error_kind k = error_kind::syntax);

	void fail_op(const char* label, std::size_t offset);

	// True when the uppercase keyword w starts at p and is not followed by a word character.
	bool keyword(const char* p, std::string_view w, bool any_case = false) const;

	// AND, OR, NOT and XOR, in any case under boolean_any_case.
	bool op_word(const char* p, std::string_view w) const;

	// An operator word where an operand should start.
	bool op_keyword(const char* p) const;

	// NEAR or ADJ at p, with or without `/n`.
	bool proximity_word(const char* p) const;
	bool slash_word(const char* p, std::string_view w) const;

	const char* step(const char* p);

	const char* binary_keyword(const char* p);

	// Does a quoted phrase end just before p?
	bool quote_before(const char* p) const;

	// A NEAR or ADJ at p that forms no chain, followed by an operator word or an expanding word,
	// makes the whole text plain words.
	bool prox_rereads(const char* p) const;

	// The operator keyword that follows r after white space, or null.
	const char* following_operator(const char* r) const;
	kind op_kind(const char* p) const;
	const char* op_label(const char* p) const;

	// A stray `)`: after an operator it reads the text as plain words; otherwise it separates
	// two parts that join with the default operator.
	void stray_ket();

	// Collects the word at p (a word character) and returns where it ends.
	const char* take_word(const char* p, const char* end, raw_word& w);

	// The glob token at p (word characters, `?` and `*`), when it holds a wildcard character.
	const char* glob_word(const char* p);

	// Does a NEAR or ADJ chain with a word operand continue after r?
	bool chain_follows(const char* r) const;

	// Does a glob token start at r?
	bool glob_follows(const char* r) const;

	// The `~n` suffix at r: the edit distance and where it ends, when it makes the word fuzzy.
	bool fuzzy_at(const char* r, std::uint32_t& dist, const char*& end) const;

	// Where the word that a run of generators at g joins to the word before it starts, or null. A
	// word that is a wildcard or an edit distance is its own item.
	const char* joins(const char* g, mark m) const;

	// Does a generator at g join the word before it to a wildcard or an edit distance?
	bool glued_anchor(const char* g, mark m) const;

	// A word, with the words that phrase generators join to it ("a-b", "title:x").
	const char* word_group(const char* p);

	// Does a plain word follow r after white space? A proximity chain cannot sit beside one.
	bool juxtaposed_word(const char* r);

	// The word ends at r: would it become a wildcard, an edit distance or a partial term?
	bool expands(const char* r, const raw_word& w) const;

	static std::size_t code_points(const std::string& s);

	// An expanding word next to NEAR or ADJ, or a mixed chain: the text is parsed again as plain words.
	const char* reparse();

	// A lone word: a term, or a wildcard, edit-distance or partial query. Returns where
	// the item ends.
	const char* single(const raw_word& w, const std::string& prefix, const char* r, const char* start);

	// A run of CJK-like characters: unigrams and bigrams ANDed at one position.
	const char* ngram_run(const char* p);

	const char* quoted(const char* p, std::string prefix = {});

	bool stop_all() const;
	bool is_stop(const raw_word& w) const;
	bool stopword(const raw_word& w) const;

	// Terms for words: a single term, or a phrase or proximity operator over them. Returns
	// no_node when every word was dropped. A proximity window is distance + terms - 1.
	node_id finish(const std::vector<raw_word>& words, const std::string& prefix, kind k, bool proximity, std::uint32_t distance);

	node_id make_term(const raw_word& w, const std::string& prefix, bool in_phrase);

	// Adds an item to the current group. n == no_node drops it, but a word that a stop list
	// removed still stands in as an operand.
	void operand(node_id n, std::size_t offset, const char* end, bool plain, bool stop = false);

	void push_item(item it);

	const char* open_frame(const char* p);

	void close_frame(const char* end);

	// Closes the current frame's operators and items into one node (no_node: nothing left).
	node_id close_group();

	// Combines the items of the current group into one node.
	node_id close_group_items();

	// l OP r: same-kind chains of AND, OR and XOR extend the left operand only. A dropped operand
	// removes its operator, and a dropped left operand removes AND_NOT with it.
	node_id join(kind k, node_id l, node_id r);

	// A NEAR, PHRASE or other n-ary node over kids.
	node_id nary(kind k, const std::vector<node_id>& kids);

	// Joins the elements of a group pairwise with the default operator.
	node_id join_default(const std::vector<node_id>& kids);

	// The words of one run join as a single n-ary node.
	node_id join_run(const std::vector<node_id>& kids);

	// The group's items become: required (+) items and, under AND, every loose element; the
	// optional rest; the excluded (-) items. A run of adjacent plain words is one element.
	node_id combine_items(std::size_t first);

	node_id combine_pass(std::size_t first, bool demote, bool& retry);

	node_id fold(const std::vector<node_id>& v, kind k);

	// Only generator characters (possibly none) lie in [from, to).
	bool gap_is_generators(std::size_t from, std::size_t to) const;

	// Only white space lies between prev and the next item (an operator word keeps its trailing generator characters).
	bool gap_is_space(const item& prev, std::size_t to) const;

	void reduce();

	const config& cfg_;
	const char* b_;
	const char* e_;
	parse_result& out_;
	builder bld_;
	std::string prefix_;
	analysis::word_scanner scanner_;
	std::vector<node_id> vals_;
	std::vector<item> items_;
	std::vector<pending_op> ops_;
	std::vector<frame> frames_;
	bool expect_ = true;
	bool failed_ = false;
	bool reparse_ = false;
	bool glob_ = false;
	bool stopped_ = false;
	bool anchor_ = false;
	bool solo_ = false;
	bool kw_ = false;
	bool quote_ = false;
	bool lone_stop_ = false;
	bool glued_ = false;
	node_id alt_ = no_node;
	bool call_ = false;  // a word glued to `(` was read
	bool ket_ = false;   // a `)` was read
	kind default_op_ = kind::or_;
	mark pending_ = mark::none;
	std::uint32_t termpos_ = 0;
	std::uint32_t dist_ = 0;
	const char* tmp_ = nullptr;
};

}  // namespace

}  // namespace queryparser

// The parser's member definitions, one header per layer.
#include "queryparser/parser_operators.hh"
#include "queryparser/parser_words.hh"
#include "queryparser/parser_groups.hh"
