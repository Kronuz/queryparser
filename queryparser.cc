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

#include "queryparser.hh"

#include "unicode.hh"

#include <cstdio>
#include <new>
#include <utility>

namespace queryparser {

namespace {

const char* op_name(kind k) noexcept {
	switch (k) {
		case kind::and_: return "AND";
		case kind::or_: return "OR";
		case kind::and_not: return "AND_NOT";
		case kind::xor_: return "XOR";
		case kind::and_maybe: return "AND_MAYBE";
		case kind::near: return "NEAR";
		case kind::phrase: return "PHRASE";
		case kind::synonym: return "SYNONYM";
		case kind::elite_set: return "ELITE_SET";
		case kind::max: return "MAX";
		default: break;
	}
	return "";
}

// Appends s with control bytes as \xNN, as Xapian's descriptions show them.
void escape(std::string& out, std::string_view s) {
	for (unsigned char c : s) {
		if (c < 0x20 || c == 0x7f) {
			static const char hex[] = "0123456789abcdef";
			out += "\\x";
			out += hex[c >> 4];
			out += hex[c & 15];
		} else {
			out += char(c);
		}
	}
}

}  // namespace

std::string query::describe() const {
	std::string out = "Query(";
	if (root_ == no_node) { return out + ")"; }
	struct frame {
		node_id id;
		node_id cursor;
		bool paren;
	};
	std::vector<frame> stack;
	// Prints a leaf, or opens the frame of an operator or scale.
	auto enter = [&](node_id id) {
		const node& n = nodes_[id];
		switch (n.k) {
			case kind::term:
				escape(out, text(n));
				out += '@';
				out += std::to_string(n.pos);
				break;
			case kind::wildcard:
				out += "WILDCARD SYNONYM ";
				escape(out, text(n));
				break;
			case kind::edit_distance:
				out += "EDIT_DISTANCE SYNONYM ";
				escape(out, text(n));
				out += '~';
				out += std::to_string(n.window);
				if (n.pos > 0) {
					out += " fixed_prefix_len=";
					out += std::to_string(n.pos);
				}
				break;
			case kind::match_all: out += "<alldocuments>"; break;
			case kind::scale: {
				char buf[32];
				std::snprintf(buf, sizeof buf, "%g", n.factor);
				out += buf;
				out += " * ";
				stack.push_back({id, n.first, false});
				break;
			}
			default:
				out += '(';
				stack.push_back({id, n.first, true});
		}
	};
	enter(root_);
	while (!stack.empty()) {
		frame& f = stack.back();
		const node& n = nodes_[f.id];
		if (f.cursor == no_node) {
			if (f.paren) { out += ')'; }
			stack.pop_back();
			continue;
		}
		const node_id child = f.cursor;
		if (child != n.first) {
			out += ' ';
			out += op_name(n.k);
			if (n.k == kind::near || n.k == kind::phrase || n.k == kind::elite_set) {
				out += ' ';
				out += std::to_string(n.window);
			}
			out += ' ';
		}
		f.cursor = nodes_[child].next;
		enter(child);
	}
	return out + ")";
}

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

	// Appends c to the children of parent; c's own `next` is cleared.
	void add(node_id parent, node_id c) {
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
		return node_id(q_.nodes_.size() - 1);
	}
	query& q_;
	std::size_t max_pool_;
};

namespace {

enum class mark : std::uint8_t { none, plus, minus };

// Internal flag of a re-read: a double quote is punctuation.
constexpr std::uint32_t plain_quotes = 1u << 31;
constexpr std::uint32_t plain_brackets = 1u << 30;

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
};

struct pending_op {
	kind k;
	std::size_t offset;
	const char* label;  // the operator as the user wrote it
	bool split;         // an implicit operator between the parts a stray `)` separates
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
	parser(std::string_view text, const config& cfg, parse_result& out)
	    : cfg_(cfg), b_(text.data()), e_(text.data() + text.size()), out_(out), bld_(out.tree, cfg.max_term_bytes), prefix_(cfg.default_prefix) {
		scanner_.cjk_ngram = cfg.flags & flag::cjk_ngram;
		default_op_ = cfg.default_op;
		switch (default_op_) {
			case kind::and_: case kind::or_: case kind::xor_: case kind::and_maybe: case kind::near: case kind::phrase:
			case kind::elite_set: case kind::synonym: case kind::max: break;
			default: default_op_ = kind::or_;
		}
		glob_ = cfg.flags & (flag::wildcard_single | flag::wildcard_multi);
	}

	bool needs_reparse() const { return reparse_; }

	void run() {
		frames_.push_back({0, 0, 0, true, mark::none, 0});
		const char* p = b_;
		while (p < e_ && !failed_) { p = step(p); }
		while (frames_.size() > 1 && !failed_) { close_frame(e_); }
		if (failed_) { return; }
		const node_id root = close_group();
		if (!failed_ && root != no_node) { bld_.set_root(root); }
	}

private:
	bool boolean() const { return cfg_.flags & flag::boolean; }
	bool phrase() const { return cfg_.flags & flag::phrase; }
	bool lovehate() const { return cfg_.flags & flag::lovehate; }
	bool has(std::uint32_t f) const { return cfg_.flags & f; }
	const frame& top() const { return frames_.back(); }

	std::size_t off(const char* p) const { return std::size_t(p - b_); }
	bool word_at(const char* p) const {
		if (p >= e_) { return false; }
		return analysis::is_word_char(unicode::next_utf8(p, e_));
	}

	void fail(std::size_t offset, std::string message, error_kind k = error_kind::syntax) {
		failed_ = true;
		out_.err = error{k, offset, std::move(message)};
	}

	void fail_op(const char* label, std::size_t offset) { fail(offset, std::string("Syntax: <expression> ") + label + " <expression>"); }

	// True when the uppercase keyword w starts at p and is not followed by a word character.
	bool keyword(const char* p, std::string_view w, bool any_case = false) const {
		if (std::size_t(e_ - p) < w.size()) { return false; }
		for (std::size_t i = 0; i < w.size(); ++i) {
			const char c = any_case && p[i] >= 'a' && p[i] <= 'z' ? char(p[i] - 'a' + 'A') : p[i];
			if (c != w[i]) { return false; }
		}
		const char* q = p + w.size();
		return !word_at(q) && !((has(flag::wildcard) || glob_) && q < e_ && *q == '*');
	}

	// AND, OR, NOT and XOR, in any case under boolean_any_case.
	bool op_word(const char* p, std::string_view w) const { return keyword(p, w, has(flag::boolean_any_case)); }

	// An operator word where an operand should start.
	bool op_keyword(const char* p) const { return op_word(p, "AND") || op_word(p, "OR") || op_word(p, "NOT") || op_word(p, "XOR"); }

	// NEAR or ADJ at p, with or without `/n`.
	bool proximity_word(const char* p) const { return keyword(p, "NEAR") || keyword(p, "ADJ") || slash_word(p, "NEAR") || slash_word(p, "ADJ"); }
	bool slash_word(const char* p, std::string_view w) const {
		return std::size_t(e_ - p) > w.size() && std::string_view(p, w.size()) == w && p[w.size()] == '/';
	}

	const char* step(const char* p) {
		const char c = *p;
		if (c == '"' && phrase() && !has(plain_quotes)) { return quoted(p); }
		if (c == '(' && !has(plain_brackets) && (boolean() || pending_ != mark::none)) { return open_frame(p); }
		if (c == ')' && !has(plain_brackets) && (boolean() || frames_.size() > 1)) {
			ket_ = true;
			if (frames_.size() > 1) {
				close_frame(p + 1);
			} else {
				stray_ket();
			}
			return p + 1;
		}
		if ((c == '+' || c == '-') && lovehate() && (p == b_ || p[-1] == ' ' || p[-1] == '(' || (p[-1] >= '\t' && p[-1] <= '\r')) && p + 1 < e_) {
			const char n = p[1];
			if (n == '"' || n == '(' || word_at(p + 1) || (glob_ && (n == '?' || n == '*'))) {
				pending_ = c == '+' ? mark::plus : mark::minus;
				return p + 1;
			}
		}
		const char* q = p;
		const char32_t c0 = unicode::next_utf8(q, e_);
		if (glob_ && (c == '?' || c == '*' || analysis::is_word_char(c0))) {
			if (const char* r = glob_word(p)) { return r; }
		}
		if (!analysis::is_word_char(c0)) { return q; }
		if (has(flag::cjk_ngram) && analysis::is_ngram_char(c0)) { return ngram_run(p); }
		if (boolean()) {
			if (const char* r = binary_keyword(p)) { return r; }
		}
		return word_group(p);
	}

	const char* binary_keyword(const char* p) {
		kind k;
		const char* label = "AND";
		const char* r = p;
		if (op_word(p, "AND")) {
			k = kind::and_;
			r = p + 3;
			const char* s = r;
			while (s < e_ && is_space(*s)) { ++s; }
			if (s > r && op_word(s, "NOT")) {
				k = kind::and_not;
				label = "AND NOT";
				r = s + 3;
			}
		} else if (op_word(p, "OR")) {
			k = kind::or_;
			label = "OR";
			r = p + 2;
		} else if (op_word(p, "NOT")) {
			k = kind::and_not;
			label = "NOT";
			r = p + 3;
		} else if (op_word(p, "XOR")) {
			k = kind::xor_;
			label = "XOR";
			r = p + 3;
		} else if (proximity_word(p)) {
			// A proximity word outside a chain is a plain word, but no operand after an operator.
			if (expect_ && ops_.size() > top().ops_base && !ops_.back().split) {
				fail_op(ops_.back().label, ops_.back().offset);
				return e_;
			}
			if (prox_rereads(p) || quote_before(p)) { return reparse(); }
			return nullptr;
		} else {
			return nullptr;
		}
		if (expect_) {
			if (k != kind::and_not || !has(flag::pure_not)) {
				// An operator with no operand before it; of two in a row, the tighter one is named.
				const bool pending = ops_.size() > top().ops_base && !ops_.back().split;
				const char* named = label;
				std::size_t at = off(p);
				kind other = k;
				if (pending) {
					other = ops_.back().k;
					named = ops_.back().label;
					at = ops_.back().offset;
					if (prec(k) > prec(other)) {
						named = label;
						at = off(p);
					}
				} else if (const char* nk = following_operator(r)) {
					if (prec(op_kind(nk)) > prec(k)) {
						named = op_label(nk);
						at = off(nk);
					}
				}
				fail_op(named, at);
				return e_;
			}
			vals_.push_back(bld_.match_all_scaled(0));
			expect_ = false;
		} else {
			const node_id g = close_group_items();
			if (failed_) { return e_; }
			vals_.push_back(g);
		}
		while (ops_.size() > top().ops_base && prec(ops_.back().k) >= prec(k)) {
			reduce();
			if (failed_) { return e_; }
		}
		ops_.push_back({k, off(p), label, false});
		expect_ = true;
		return r;
	}

	// Does a quoted phrase end just before p?
	bool quote_before(const char* p) const {
		if (!phrase() || expect_ || items_.size() <= top().items_base) { return false; }
		const item& last = items_.back();
		return last.quote && gap_is_space(last, off(p));
	}

	// A NEAR or ADJ at p that forms no chain, followed by an operator word or an expanding word,
	// makes the whole text plain words.
	bool prox_rereads(const char* p) const {
		const char* s = p + (keyword(p, "NEAR") || slash_word(p, "NEAR") ? 4 : 3);
		bool explicit_n = false;
		if (s < e_ && *s == '/') {
			const char* d = s + 1;
			bool zeros = true;
			for (; d < e_ && *d >= '0' && *d <= '9'; ++d) { zeros = zeros && *d == '0'; }
			if (d == s + 1 || word_at(d) || zeros) { return false; }
			s = d;
			explicit_n = true;
		} else if (word_at(s)) {
			return false;
		}
		const char* x = s;
		while (x < e_ && is_space(*x)) { ++x; }
		if ((x == s && !explicit_n) || x >= e_) { return false; }
		if (lovehate() && (*x == '+' || *x == '-') && x + 1 < e_ && word_at(x + 1)) { return true; }
		if (*x == '"' && phrase()) { return true; }
		if (!word_at(x)) { return false; }
		if (op_keyword(x)) { return true; }
		if (proximity_word(x)) { return false; }
		raw_word w;
		const char* u = const_cast<parser*>(this)->take_word(x, e_, w);
		for (;;) {
			if (expands(u, w)) { return true; }
			const char* g = u;
			while (g < e_ && is_generator(*g)) { ++g; }
			if (!phrase() || g == u || !word_at(g)) { return false; }
			u = const_cast<parser*>(this)->take_word(g, e_, w);
		}
	}

	// The operator keyword that follows r after white space, or null.
	const char* following_operator(const char* r) const {
		while (r < e_ && is_space(*r)) { ++r; }
		return r < e_ && op_keyword(r) ? r : nullptr;
	}
	kind op_kind(const char* p) const {
		if (op_word(p, "OR")) { return kind::or_; }
		if (op_word(p, "XOR")) { return kind::xor_; }
		return kind::and_;  // AND, AND NOT and NOT bind alike
	}
	const char* op_label(const char* p) const {
		if (op_word(p, "OR")) { return "OR"; }
		if (op_word(p, "XOR")) { return "XOR"; }
		if (op_word(p, "NOT")) { return "NOT"; }
		const char* s = p + 3;
		while (s < e_ && is_space(*s)) { ++s; }
		return s > p + 3 && op_word(s, "NOT") ? "AND NOT" : "AND";
	}

	// A stray `)`: after an operator it reads the text as plain words; otherwise it separates
	// two parts that join with the default operator.
	void stray_ket() {
		if (expect_ && ops_.size() > top().ops_base && !ops_.back().split) {
			fail_op(ops_.back().label, ops_.back().offset);
			return;
		}
		reparse();
	}

	// Collects the word at p (a word character) and returns where it ends.
	const char* take_word(const char* p, const char* end, raw_word& w) {
		const char* q = p;
		const char32_t c0 = unicode::next_utf8(q, end);
		w.capital = unicode::category_of(c0) == unicode::category::uppercase_letter || unicode::category_of(c0) == unicode::category::titlecase_letter;
		const char* r = scanner_.scan(p, end);
		const bool call = r < end && *r == '(';
		w.text = scanner_.text();
		// A `+` or `#` suffix belongs to a one-character word only (`c++`, `c#`).
		std::size_t base = w.text.size();
		while (base > 0 && (w.text[base - 1] == '+' || w.text[base - 1] == '#')) { --base; }
		if (!call && base < w.text.size() && base > 0 && code_points(w.text.substr(0, base)) != 1 && r - p >= std::ptrdiff_t(w.text.size() - base)) {
			r -= w.text.size() - base;
			w.text.resize(base);
		}
		return r;
	}

	// The glob token at p (word characters, `?` and `*`), when it holds a wildcard character.
	const char* glob_word(const char* p) {
		const char* r = p;
		bool wild = false;
		std::string pat;
		while (r < e_) {
			const char* q = r;
			const char32_t c = unicode::next_utf8(q, e_);
			if (*r == '?' || *r == '*') {
				wild = true;
				pat += *r;
			} else if (analysis::is_word_char(c)) {
				unicode::append_utf8(pat, unicode::tolower(c));
			} else {
				break;
			}
			r = q;
		}
		if (!wild || pat.size() < 2 || pending_ == mark::minus) { return nullptr; }
		if (boolean() && chain_follows(r)) {
			reparse_ = true;
			failed_ = true;
			return e_;
		}
		anchor_ = true;
		++termpos_;
		operand(bld_.leaf(kind::wildcard, prefix_ + pat, 0, cfg_.wildcard_expansion), off(p), r, false);
		return r;
	}

	// Does a NEAR or ADJ chain with a word operand continue after r?
	bool chain_follows(const char* r) const {
		const char* s = r;
		while (s < e_ && is_space(*s)) { ++s; }
		if (s == r || !proximity_word(s)) { return false; }
		s += keyword(s, "NEAR") || slash_word(s, "NEAR") ? 4 : 3;
		if (s < e_ && *s == '/') {
			++s;
			while (s < e_ && *s >= '0' && *s <= '9') { ++s; }
		}
		while (s < e_ && is_space(*s)) { ++s; }
		return s < e_ && word_at(s) && !proximity_word(s) && !op_keyword(s);
	}

	// Does a glob token start at r?
	bool glob_follows(const char* r) const {
		if (!glob_) { return false; }
		while (r < e_) {
			if (*r == '?' || *r == '*') { return true; }
			const char* q = r;
			if (!analysis::is_word_char(unicode::next_utf8(q, e_))) { return false; }
			r = q;
		}
		return false;
	}

	// The `~n` suffix at r: the edit distance and where it ends, when it makes the word fuzzy.
	bool fuzzy_at(const char* r, std::uint32_t& dist, const char*& end) const {
		if (!has(flag::fuzzy) || r >= e_ || *r != '~') { return false; }
		const char* j = r + 1;
		const char* d = j;
		std::uint64_t v = 0;
		while (d < e_ && *d >= '0' && *d <= '9') {
			v = v * 10 + std::uint64_t(*d - '0');
			if (v > 0xFFFFFFFFu) { v = 0xFFFFFFFFu; }
			++d;
		}
		if (d == j && !word_at(j) && !(j < e_ && *j == '~')) {
			dist = 2;
			end = j;
			return true;
		}
		if (d > j && d == e_) {
			dist = std::uint32_t(v);
			end = d;
			return true;
		}
		return false;
	}

	// Where the word that a run of generators at g joins to the word before it starts, or null. A
	// word that is a wildcard or an edit distance is its own item.
	const char* joins(const char* g, mark m) const {
		const char* w = g;
		while (w < e_ && is_generator(*w)) { ++w; }
		if (!phrase() || w == g || !word_at(w)) { return nullptr; }
		raw_word probe;
		const char* u = const_cast<parser*>(this)->take_word(w, e_, probe);
		std::uint32_t d;
		const char* x;
		if (m != mark::minus && ((has(flag::wildcard) && u < e_ && *u == '*') || glob_follows(w) || fuzzy_at(u, d, x))) { return nullptr; }
		return w;
	}

	// A word, with the words that phrase generators join to it ("a-b", "title:x").
	const char* word_group(const char* p) {
		std::vector<raw_word> words;
		std::string prefix = prefix_;
		const mark m = pending_;
		raw_word w;
		const char* r = take_word(p, e_, w);
		if (r < e_ && *r == ':' && cfg_.resolver) {
			field_spec spec;
			if (cfg_.resolver(std::string_view(p, std::size_t(r - p)), spec)) {
				const char* s = r + 1;
				if (s < e_ && *s == '"' && phrase()) { return quoted(s, std::move(spec.prefix)); }
				if (word_at(s)) {
					prefix = std::move(spec.prefix);
					r = take_word(s, e_, w);
				} else {
					return s;
				}
			}
		}
		words.push_back(std::move(w));
		bool generated = false;
		// A generator joins the next word unless that word is a wildcard or an edit distance.
		while (const char* g = joins(r, m)) {
			raw_word nw;
			r = take_word(g, e_, nw);
			words.push_back(std::move(nw));
			generated = true;
		}
		if (r < e_ && std::string_view("*=<>[{/@\\\"(").find(*r) != std::string_view::npos) { words.back().star = true; }
		if (r < e_ && *r == '(') { call_ = true; }
		// NEAR and ADJ chains: words only, with no other item beside the chain.
		kind prox = kind::term;
		std::uint32_t distance = 10;
		bool proximity = false, set_distance = false;
		while (boolean() && !generated) {
			const char* s0 = r;
			if (r < e_ && *r == '*') {
				s0 = r + 1;
			} else if (std::uint32_t d; fuzzy_at(r, d, tmp_)) {
				s0 = tmp_;
			}
			const char* s = s0;
			while (s < e_ && is_space(*s)) { ++s; }
			if (s == s0) { break; }
			kind k;
			if (keyword(s, "NEAR") || slash_word(s, "NEAR")) {
				k = kind::near;
			} else if (keyword(s, "ADJ") || slash_word(s, "ADJ")) {
				k = kind::phrase;
			} else {
				break;
			}
			const char* t = s + (k == kind::near ? 4 : 3);
			std::uint32_t n = 10;
			bool explicit_n = false;
			if (t < e_ && *t == '/') {
				const char* u = t + 1;
				std::uint32_t v = 0;
				bool zeros = true;
				while (u < e_ && *u >= '0' && *u <= '9') {
					v = v * 10 + std::uint32_t(*u - '0');  // wraps like a 32-bit integer
					zeros = zeros && *u == '0';
					++u;
				}
				if (u == t + 1 || word_at(u) || zeros) { break; }
				n = v == 0 ? 10 : v;
				explicit_n = true;
				t = u;
			} else if (word_at(t)) {
				break;
			}
			const char* x = t;
			while (x < e_ && is_space(*x)) { ++x; }
			if (x == t && !explicit_n) { break; }
			// A lone `+` between the operator and its operand is skipped.
			if (x + 1 < e_ && *x == '+' && is_space(x[1])) {
				for (x += 2; x < e_ && is_space(*x);) { ++x; }
			}
			// No operand, or one that is no plain word: NEAR stays a word.
			if (x >= e_ || !word_at(x) || proximity_word(x) || op_keyword(x)) { break; }
			raw_word nw;
			const char* r2 = take_word(x, e_, nw);
			if (joins(r2, m)) { break; }
			if (prox != kind::term && prox != k) { return reparse(); }
			if (!proximity && (!expect_ || m == mark::minus)) { return reparse(); }
			if (expands(r, words.back()) || expands(r2, nw)) { return reparse(); }
			prox = k;
			if (explicit_n || !set_distance) { distance = n; }
			set_distance = set_distance || explicit_n;
			proximity = true;
			words.push_back(std::move(nw));
			r = r2;
		}
		if (proximity && juxtaposed_word(r)) { return reparse(); }
		if (proximity) {
			operand(finish(words, prefix, prox, true, distance), off(p), r, false);
		} else if (!generated && words[0].text.size() <= cfg_.max_word_length) {
			r = single(words[0], prefix, r, p);
		} else {
			operand(finish(words, prefix, generated ? kind::phrase : kind::term, false, 0), off(p), r, false);
		}
		return r;
	}

	// Does a plain word follow r after white space? A proximity chain cannot sit beside one.
	bool juxtaposed_word(const char* r) {
		const char* s = r;
		while (s < e_ && is_space(*s)) { ++s; }
		if (s == r || s >= e_ || !word_at(s) || op_keyword(s) || proximity_word(s)) { return false; }
		raw_word w;
		const char* u = take_word(s, e_, w);
		return !(has(flag::wildcard) && u < e_ && *u == '*') && !glob_follows(s) && !fuzzy_at(u, dist_, tmp_);
	}

	// The word ends at r: would it become a wildcard, an edit distance or a partial term?
	bool expands(const char* r, const raw_word& w) const {
		std::uint32_t d;
		const char* x;
		if (has(flag::wildcard) && r < e_ && *r == '*') { return true; }
		if (fuzzy_at(r, d, x)) { return true; }
		if (glob_follows(r)) { return true; }
		return has(flag::partial) && r == e_ && code_points(w.text) >= 2;
	}

	static std::size_t code_points(const std::string& s) {
		std::size_t n = 0;
		for (unsigned char c : s) { n += (c & 0xC0) != 0x80; }
		return n;
	}

	// An expanding word next to NEAR or ADJ, or a mixed chain: the text is parsed again as plain words.
	const char* reparse() {
		reparse_ = true;
		failed_ = true;
		return e_;
	}

	// A lone word: a term, or a wildcard, edit-distance or partial query. Returns where
	// the item ends.
	const char* single(const raw_word& w, const std::string& prefix, const char* r, const char* start) {
		const bool expand = pending_ != mark::minus;
		if (expand && has(flag::wildcard) && r < e_ && *r == '*') {
			anchor_ = true;
			++termpos_;
			operand(bld_.leaf(kind::wildcard, prefix + w.text, 0, cfg_.wildcard_expansion), off(start), r + 1, false);
			return r + 1;
		}
		std::uint32_t dist;
		const char* fend;
		if (expand && fuzzy_at(r, dist, fend)) {
			const node_id n = bld_.leaf(kind::edit_distance, prefix + w.text, dist, cfg_.fuzzy_expansion);
			bld_.set_pos(n, std::uint32_t(prefix.size()));
			anchor_ = true;
			++termpos_;
			operand(n, off(start), fend, false);
			return fend;
		}
		if (has(flag::partial) && !(call_ && ket_) && r == e_ && code_points(w.text) >= 2 && !(boolean() && (keyword(start, "NEAR") || keyword(start, "ADJ")))) {
			const node_id wc = bld_.leaf(kind::wildcard, prefix + w.text, 0, cfg_.partial_expansion);
			anchor_ = true;
			const bool stopped = cfg_.stopper && is_stop(w);
			const node_id t = make_term(w, prefix, false);
			alt_ = t;
			if (stopped) {
				operand(wc, off(start), r, false, true);
				return r;
			}
			const node_id pair = bld_.op(kind::or_, 0, true);
			bld_.add(pair, wc);
			bld_.add(pair, t);
			operand(pair, off(start), r, false);
			return r;
		}
		std::vector<raw_word> one{w};
		const bool solo = r + 1 < e_ && ((has(flag::fuzzy) && *r == '~') || *r == '*');
		const bool stop = cfg_.stopper && !stop_all() && is_stop(w);
		solo_ = solo;
		kw_ = w.capital && (w.text == "and" || w.text == "or" || w.text == "not" || w.text == "xor" || w.text == "near" || w.text == "adj");
		operand(finish(one, prefix, kind::term, false, 0), off(start), r, !solo, stop);
		return r;
	}

	// A run of CJK-like characters: unigrams and bigrams ANDed at one position.
	const char* ngram_run(const char* p) {
		std::vector<std::string> units;
		while (p < e_) {
			const char* q = p;
			const char32_t c = unicode::next_utf8(q, e_);
			if (!analysis::is_word_char(c) || !analysis::is_ngram_char(c)) { break; }
			units.emplace_back();
			unicode::append_utf8(units.back(), unicode::tolower(c));
			p = q;
		}
		const std::uint32_t pos = ++termpos_;
		node_id res = no_node;
		auto add = [&](std::string_view t) {
			const node_id n = bld_.term(prefix_ + std::string(t), pos);
			res = res == no_node ? n : bld_.combine(kind::and_, res, n);
		};
		for (std::size_t i = 0; i < units.size(); ++i) {
			if (units[i].size() <= cfg_.max_word_length) { add(units[i]); }
			if (i + 1 < units.size()) { add(units[i] + units[i + 1]); }
		}
		operand(res, std::size_t(p - b_), p, false);
		return p;
	}

	const char* quoted(const char* p, std::string prefix = {}) {
		if (prefix.empty()) { prefix = prefix_; }
		const char* s = p + 1;
		const char* q = s;
		// A doubled quote inside a phrase is part of it.
		while (q < e_ && *q != '"') { ++q; }
		while (q > s && q + 1 < e_ && q[1] == '"') {
			q += 2;
			while (q < e_ && *q != '"') { ++q; }
		}
		// An unclosed phrase that holds a glob character is read as plain words.
		if (q >= e_ && glob_ && std::string_view(s, std::size_t(q - s)).find_first_of("*?") != std::string_view::npos) { return reparse(); }
		std::vector<raw_word> words;
		const char* t = s;
		while (t < q) {
			const char* u = t;
			if (analysis::is_word_char(unicode::next_utf8(u, q))) {
				raw_word w;
				t = take_word(t, q, w);
				words.push_back(std::move(w));
			} else {
				t = u;
			}
		}
		const char* after = q < e_ ? q + 1 : q;
		quote_ = true;
		operand(finish(words, prefix, kind::phrase, false, 0), off(p), after, false);
		return after;
	}

	bool stop_all() const { return cfg_.stopper && cfg_.stop == analysis::stop_strategy::all; }
	bool is_stop(const raw_word& w) const { return cfg_.stopper && (*cfg_.stopper)(w.text); }

	// Terms for words: a single term, or a phrase or proximity operator over them. Returns
	// no_node when every word was dropped. A proximity window is distance + terms - 1.
	node_id finish(const std::vector<raw_word>& words, const std::string& prefix, kind k, bool proximity, std::uint32_t distance) {
		std::vector<node_id> terms;
		terms.reserve(words.size());
		std::size_t counted = 0;
		stopped_ = false;
		for (const auto& w : words) {
			if (w.text.size() > cfg_.max_word_length) { continue; }
			++counted;
			if (stop_all() && is_stop(w)) {
				++termpos_;
				continue;
			}
			terms.push_back(make_term(w, prefix, k != kind::term));
		}
		if (terms.empty()) {
			stopped_ = counted > 0;
			return no_node;
		}
		if (terms.size() == 1) { return terms[0]; }
		const std::uint32_t count = std::uint32_t(counted);
		std::uint32_t window = proximity ? distance + (count - 1) : count;  // 32-bit wrap
		if (window < count) { window = count; }
		const node_id n = bld_.op(k, window);
		for (node_id t : terms) { bld_.add(n, t); }
		return n;
	}

	node_id make_term(const raw_word& w, const std::string& prefix, bool in_phrase) {
		using analysis::stem_strategy;
		const stem_strategy s = cfg_.stem;
		std::string t;
		auto stem = [&](std::string_view word) { return std::string((*cfg_.stemmer)(word)); };
		bool z = false;
		std::string body = w.text;
		if (cfg_.stemmer) {
			if (s == stem_strategy::all || s == stem_strategy::all_z) {
				body = stem(w.text);
				z = s == stem_strategy::all_z;
			} else if (s == stem_strategy::some && !in_phrase && !w.capital && !w.star) {
				const char* q = w.text.data();
				if (unicode::is_letter(unicode::next_utf8(q, q + w.text.size()))) {
					body = stem(w.text);
					z = true;
				}
			}
		}
		if (z) { t = "Z"; }
		t += prefix;
		t += body;
		return bld_.term(t, ++termpos_);
	}

	// Adds an item to the current group. n == no_node drops it, but a word that a stop list
	// removed still stands in as an operand.
	void operand(node_id n, std::size_t offset, const char* end, bool plain, bool stop = false) {
		const mark m = std::exchange(pending_, mark::none);
		if (n == no_node && !std::exchange(stopped_, false)) { return; }
		push_item({n, m, offset, std::size_t(end - b_), plain, stop, anchor_, solo_, kw_, quote_, false, std::exchange(alt_, no_node)});
		anchor_ = solo_ = kw_ = quote_ = false;
	}

	void push_item(item it) {
		if (expect_) {
			if (it.m != mark::none && ops_.size() > top().ops_base && !ops_.back().split) {
				// The right operand of an explicit operator: `a AND -b` is `a AND_NOT b`, a `+` adds
				// nothing, and a `-` after OR, XOR or NOT would turn an exclusion into an inclusion.
				pending_op& o = ops_.back();
				if (it.m == mark::minus) {
					if (o.k != kind::and_) {
						fail_op(o.label, o.offset);
						return;
					}
					o.k = kind::and_not;
				}
				it.m = mark::none;
			}
			expect_ = false;
		}
		items_.push_back(it);
	}

	const char* open_frame(const char* p) {
		frames_.push_back({ops_.size(), vals_.size(), items_.size(), expect_, std::exchange(pending_, mark::none), off(p)});
		expect_ = true;
		return p + 1;
	}

	void close_frame(const char* end) {
		const frame f = frames_.back();
		const bool had = !expect_ || vals_.size() > f.vals_base || ops_.size() > f.ops_base;
		const node_id n = close_group();
		if (failed_) { return; }
		frames_.pop_back();
		expect_ = f.parent_expect;
		if (had) { push_item({n, f.m, f.offset, off(end), false, std::exchange(lone_stop_, false), false, false, false, false, true}); }
	}

	// Closes the current frame's operators and items into one node (no_node: nothing left).
	node_id close_group() {
		const frame f = top();
		if (expect_ && ops_.size() > f.ops_base) {
			if (!ops_.back().split) {
				fail_op(ops_.back().label, ops_.back().offset);
				return no_node;
			}
			ops_.pop_back();
		}
		if (!expect_) {
			// A bracket that holds one stop word and nothing else.
			lone_stop_ = ops_.size() == f.ops_base && vals_.size() == f.vals_base && items_.size() == f.items_base + 1 && items_.back().stop && items_.back().m == mark::none;
			const node_id g = close_group_items();
			if (failed_) { return no_node; }
			vals_.push_back(g);
		}
		while (ops_.size() > f.ops_base) {
			reduce();
			if (failed_) { return no_node; }
		}
		node_id res = no_node;
		if (vals_.size() > f.vals_base) { res = vals_.back(); }
		vals_.resize(f.vals_base);
		return res;
	}

	// Combines the items of the current group into one node.
	node_id close_group_items() {
		const std::size_t first = top().items_base;
		const node_id res = combine_items(first);
		items_.resize(first);
		return res;
	}

	// l OP r: same-kind chains of AND, OR and XOR extend the left operand only. A dropped operand
	// removes its operator, and a dropped left operand removes AND_NOT with it.
	node_id join(kind k, node_id l, node_id r) {
		if (l == no_node) { return k == kind::and_not ? no_node : r; }
		if (r == no_node) { return l; }
		switch (k) {
			case kind::near: case kind::phrase: case kind::elite_set: case kind::synonym: case kind::max: return nary(k, {l, r});
			default: return bld_.combine(k, l, r);
		}
	}

	// A NEAR, PHRASE or other n-ary node over kids.
	node_id nary(kind k, const std::vector<node_id>& kids) {
		if (kids.size() == 1) { return kids[0]; }
		std::uint32_t window = 0;
		if (k == kind::near || k == kind::phrase) {
			for (node_id c : kids) {
				if (bld_.at(c).k != kind::term) {
					fail(0, "OP_NEAR and OP_PHRASE only currently support leaf subqueries", error_kind::unsupported);
					return no_node;
				}
			}
			window = std::uint32_t(10 + kids.size() - 1);
		} else if (k == kind::elite_set) {
			window = 10;
		}
		const node_id n = bld_.op(k, window);
		for (node_id c : kids) { bld_.add(n, c); }
		return n;
	}

	// Joins the elements of a group pairwise with the default operator.
	node_id join_default(const std::vector<node_id>& kids) {
		if (kids.empty()) { return no_node; }
		node_id acc = kids[0];
		for (std::size_t i = 1; i < kids.size(); ++i) { acc = join(default_op_, acc, kids[i]); }
		return acc;
	}

	// The words of one run join as a single n-ary node.
	node_id join_run(const std::vector<node_id>& kids) {
		switch (default_op_) {
			case kind::near: case kind::phrase: case kind::elite_set: case kind::synonym: case kind::max: return nary(default_op_, kids);
			default: return join_default(kids);
		}
	}

	// The group's items become: required (+) items and, under AND, every loose element; the
	// optional rest; the excluded (-) items. A run of adjacent plain words is one element.
	node_id combine_items(std::size_t first) {
		if (items_.size() - first == 1 && (items_[first].m == mark::none || ((cfg_.stopper || !boolean()) && items_[first].m == mark::minus))) {
			return items_[first].m == mark::minus && items_[first].alt != no_node ? items_[first].alt : items_[first].n;
		}
		// With a stop list, a group of only exclusions reads as if none were marked.
		const std::vector<item> saved(items_.begin() + std::ptrdiff_t(first), items_.end());
		bool retry = false;
		node_id res = combine_pass(first, false, retry);
		if (retry && !failed_) {
			std::copy(saved.begin(), saved.end(), items_.begin() + std::ptrdiff_t(first));
			res = combine_pass(first, true, retry);
		}
		return res;
	}

	node_id combine_pass(std::size_t first, bool demote, bool& retry) {
		const std::size_t end = items_.size();
		if (demote) {
			for (std::size_t i = first; i < end; ++i) {
				item& x = items_[i];
				if (x.m != mark::minus) { continue; }
				x.m = mark::none;
				if (x.alt != no_node) {
					x.n = x.alt;
					x.plain = true;
				}
			}
		}
		// A bracket of one stop word goes beside a heavy item (a non-stop word, or an operator word)
		// that white space separates from it, or after a word glued to it.
		if (end - first > 1) {
			std::vector<bool> drop(end - first, false);
			for (std::size_t i = first; i < end; ++i) {
				const item& x = items_[i];
				if (!x.stop || !x.group || x.m != mark::none || x.n == no_node) { continue; }
				auto heavy = [&](const item& y) { return !y.group && y.n != no_node && (y.kw || !y.stop); };
				bool d = false;
				if (i > first) {
					const item& y = items_[i - 1];
					d |= heavy(y) && y.end < x.offset && gap_is_space(y, x.offset);
					d |= (y.plain || y.solo) && y.n != no_node && gap_is_generators(y.end, x.offset);
				}
				if (i + 1 < end) {
					const item& y = items_[i + 1];
					d |= heavy(y) && x.end < y.offset && gap_is_space(x, y.offset);
				}
				drop[i - first] = d;
			}
			for (std::size_t i = first; i < end; ++i) {
				if (drop[i - first]) { items_[i].n = no_node; }
			}
		}
		// Stop words go from a run, unless the run is only stop words and has two or more.
		std::vector<std::size_t> run_end(end - first, 0);
		bool anchored = false;
		for (std::size_t x = first; x < end; ++x) { anchored |= items_[x].anchor; }
		for (std::size_t i = first; i < end;) {
			std::size_t j = i + 1;
			if ((items_[i].plain || items_[i].solo) && items_[i].m == mark::none) {
				while (!items_[i].solo && j < end && items_[j].plain && items_[j].m == mark::none && gap_is_space(items_[j - 1], items_[j].offset)) { ++j; }
				bool nonstop = false;
				for (std::size_t x = i; x < j; ++x) { nonstop |= !items_[x].stop; }
				if (nonstop || j - i < 2 || anchored) {
					for (std::size_t x = i; x < j; ++x) {
						if (items_[x].stop) { items_[x].n = no_node; }
					}
				}
			}
			run_end[i - first] = j;
			i = j;
		}
		std::vector<node_id> req, opt, exc;
		const bool all_required = default_op_ == kind::and_;
		if ((cfg_.stopper || !boolean()) && !demote) {
			// Exclusions need a positive item that is no stop word.
			bool hate = false, solid = false;
			for (std::size_t i = first; i < end; ++i) {
				const item& x = items_[i];
				hate |= x.m == mark::minus;
				solid |= x.n != no_node && x.m != mark::minus;
			}
			if (hate && !solid) {
				retry = true;
				return no_node;
			}
		}
		for (std::size_t i = first; i < end;) {
			const item& x = items_[i];
			const std::size_t j = run_end[i - first];
			if (x.m == mark::plus) {
				if (x.n != no_node) { req.push_back(x.n); }
			} else if (x.m == mark::minus) {
				if (x.n != no_node) { exc.push_back(x.n); }
			} else {
				std::vector<node_id> kids;
				for (std::size_t y = i; y < j; ++y) {
					if (items_[y].n != no_node) { kids.push_back(items_[y].n); }
				}
				const node_id el = kids.size() <= 1 ? (kids.empty() ? no_node : kids[0]) : (all_required ? fold(kids, kind::and_) : join_run(kids));
				if (failed_) { return no_node; }
				if (el != no_node) { (all_required ? req : opt).push_back(el); }
			}
			i = j;
		}
		node_id res = fold(req, kind::and_);
		const node_id o = opt.empty() ? no_node : join_default(opt);
		if (failed_) { return no_node; }
		if (res == no_node) {
			res = o;
		} else if (o != no_node) {
			res = bld_.combine(kind::and_maybe, res, o);
		}
		const node_id x = fold(exc, kind::or_);
		if (x != no_node) {
			if (res == no_node && has(flag::pure_not)) { res = bld_.match_all_scaled(0); }
			if (res == no_node && (cfg_.stopper || !boolean()) && !demote) {
				retry = true;
				return no_node;
			}
			if (res == no_node) {
				fail_op("NOT", items_[first].offset);
				return no_node;
			}
			res = bld_.combine(kind::and_not, res, x);
		}
		return res;
	}

	node_id fold(const std::vector<node_id>& v, kind k) {
		node_id acc = no_node;
		for (node_id n : v) { acc = acc == no_node ? n : bld_.combine(k, acc, n); }
		return acc;
	}

	// Only generator characters (possibly none) lie in [from, to).
	bool gap_is_generators(std::size_t from, std::size_t to) const {
		for (; from < to; ++from) {
			if (!is_generator(b_[from])) { return false; }
		}
		return true;
	}

	// Only white space lies between prev and the next item (an operator word keeps its trailing generator characters).
	bool gap_is_space(const item& prev, std::size_t to) const {
		std::size_t from = prev.end;
		while (prev.kw && from < to && is_generator(b_[from])) { ++from; }
		while (from < to) {
			const char* q = b_ + from;
			const char32_t c = unicode::next_utf8(q, b_ + to);
			const auto cat = unicode::category_of(c);
			if (!is_space(b_[from]) && cat != unicode::category::space_separator && cat != unicode::category::control) { return false; }
			from = std::size_t(q - b_);
		}
		return true;
	}

	void reduce() {
		const pending_op o = ops_.back();
		ops_.pop_back();
		const node_id r = vals_.back();
		vals_.pop_back();
		node_id& l = vals_.back();
		l = join(o.k, l, r);
	}

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

parse_result parse(std::string_view text, const config& cfg) {
	parse_result res;
	if (text.size() > max_query_bytes) {
		res.err = error{error_kind::too_long, 0, "Query too long"};
		return res;
	}
	try {
		parser first(text, cfg, res);
		first.run();
		if (first.needs_reparse()) {
			// Plain words: no operators and no expansion.
			config plain = cfg;
			plain.flags |= plain_quotes | plain_brackets;
			plain.flags &= ~(flag::lovehate | flag::boolean | flag::wildcard | flag::partial | flag::fuzzy | flag::wildcard_single | flag::wildcard_multi);
			res = parse_result{};
			parser(text, plain, res).run();
		}
	} catch (const limit_exceeded&) {
		res = parse_result{};
		res.err = error{error_kind::too_long, 0, "Query too long"};
	} catch (const std::bad_alloc&) {
		res = parse_result{};
		res.err = error{error_kind::out_of_memory, 0, "Out of memory"};
	}
	return res;
}

}  // namespace queryparser
