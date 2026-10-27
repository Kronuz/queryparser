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
		case kind::term: break;
	}
	return "";
}

}  // namespace

std::string query::describe() const {
	std::string out = "Query(";
	if (root_ == no_node) { return out + ")"; }
	struct frame {
		node_id id;
		node_id cursor;
	};
	auto term = [&](const node& n) {
		out += text(n);
		out += '@';
		out += std::to_string(n.pos);
	};
	if (nodes_[root_].k == kind::term) {
		term(nodes_[root_]);
		return out + ")";
	}
	std::vector<frame> stack;
	stack.push_back({root_, nodes_[root_].first});
	out += '(';
	while (!stack.empty()) {
		frame& f = stack.back();
		const node& n = nodes_[f.id];
		if (f.cursor == no_node) {
			out += ')';
			stack.pop_back();
			continue;
		}
		const node_id child = f.cursor;
		if (child != n.first) {
			out += ' ';
			out += op_name(n.k);
			if (n.k == kind::near || n.k == kind::phrase) {
				out += ' ';
				out += std::to_string(n.window);
			}
			out += ' ';
		}
		f.cursor = nodes_[child].next;
		const node& c = nodes_[child];
		if (c.k == kind::term) {
			term(c);
		} else {
			out += '(';
			stack.push_back({child, c.first});
		}
	}
	return out + ")";
}

// Builds a query's nodes.
class builder {
public:
	explicit builder(query& q) : q_(q) {}

	node_id term(std::string_view text, std::uint32_t pos) {
		node n;
		n.pos = pos;
		n.text_off = std::uint32_t(q_.pool_.size());
		n.text_len = std::uint32_t(text.size());
		q_.pool_.append(text);
		return push(n);
	}

	node_id op(kind k, std::uint32_t window = 0) {
		node n;
		n.k = k;
		n.window = window;
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

	// Appends the children of src (an op node) to parent, in order.
	void splice(node_id parent, node_id src) {
		const node_id f = q_.nodes_[src].first;
		if (f == no_node) { return; }
		node& p = q_.nodes_[parent];
		if (p.last == no_node) {
			p.first = f;
		} else {
			q_.nodes_[p.last].next = f;
		}
		q_.nodes_[parent].last = q_.nodes_[src].last;
		q_.nodes_[src].first = q_.nodes_[src].last = no_node;
	}

	// l OP r, where AND, OR and XOR chains stay one n-ary node.
	node_id combine(kind k, node_id l, node_id r) {
		const bool flat = k == kind::and_ || k == kind::or_ || k == kind::xor_;
		node_id res;
		if (flat && q_.nodes_[l].k == k) {
			res = l;
		} else {
			res = op(k);
			add(res, l);
		}
		if (flat && q_.nodes_[r].k == k) {
			splice(res, r);
		} else {
			add(res, r);
		}
		return res;
	}

	const node& at(node_id id) const { return q_.nodes_[id]; }
	void set_root(node_id id) { q_.root_ = id; }
	void reserve(std::size_t n) { q_.nodes_.reserve(n); }

private:
	node_id push(const node& n) {
		q_.nodes_.push_back(n);
		return node_id(q_.nodes_.size() - 1);
	}
	query& q_;
};

namespace {

enum class mark : std::uint8_t { none, plus, minus };

struct item {
	node_id n;
	mark m;
	std::size_t offset;
};

struct pending_op {
	kind k;
	std::size_t offset;
	const char* label;  // the operator as the user wrote it
};

struct frame {
	std::size_t ops_base;
	std::size_t vals_base;
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
		default: return 3;  // and_, and_not
	}
}

constexpr bool is_generator(char c) noexcept { return c == '-' || c == ':' || c == '/' || c == '.' || c == '@' || c == '\\'; }

class parser {
public:
	parser(std::string_view text, const config& cfg, parse_result& out)
	    : cfg_(cfg), b_(text.data()), e_(text.data() + text.size()), out_(out), bld_(out.tree) {
		scanner_.cjk_ngram = false;
	}

	void run() {
		frames_.push_back({0, 0, true, mark::none, 0});
		const char* p = b_;
		while (p < e_ && !failed_) { p = step(p); }
		while (frames_.size() > 1 && !failed_) { close_frame(); }
		if (failed_) { return; }
		const node_id root = close_group();
		if (!failed_) { bld_.set_root(root); }
	}

private:
	bool boolean() const { return cfg_.flags & flag::boolean; }
	bool phrase() const { return cfg_.flags & flag::phrase; }
	bool lovehate() const { return cfg_.flags & flag::lovehate; }

	std::size_t off(const char* p) const { return std::size_t(p - b_); }
	bool word_at(const char* p) const {
		if (p >= e_) { return false; }
		return analysis::is_word_char(unicode::next_utf8(p, e_));
	}

	void fail(std::size_t offset, std::string message) {
		failed_ = true;
		out_.err = error{error_kind::syntax, offset, std::move(message)};
	}

	void fail_op(const char* label, std::size_t offset) { fail(offset, std::string("Syntax: <expression> ") + label + " <expression>"); }

	// True when the uppercase keyword w starts at p and is not followed by a word character.
	bool keyword(const char* p, std::string_view w) const {
		if (std::size_t(e_ - p) < w.size() || std::string_view(p, w.size()) != w) { return false; }
		return !word_at(p + w.size());
	}

	const char* step(const char* p) {
		const char c = *p;
		if (c == '"' && phrase()) { return quoted(p); }
		if (c == '(' && boolean()) { return open_frame(p); }
		if (c == ')' && boolean()) {
			if (frames_.size() > 1) { close_frame(); }
			return p + 1;
		}
		if ((c == '+' || c == '-') && lovehate() && (p == b_ || p[-1] == ' ' || p[-1] == '(' || (p[-1] >= '\t' && p[-1] <= '\r')) && p + 1 < e_) {
			const char n = p[1];
			if (n == '"' || n == '(' || word_at(p + 1)) {
				pending_ = c == '+' ? mark::plus : mark::minus;
				return p + 1;
			}
		}
		const char* q = p;
		if (!analysis::is_word_char(unicode::next_utf8(q, e_))) { return q; }
		if (boolean()) {
			if (const char* r = binary_keyword(p)) { return r; }
		}
		return word_group(p);
	}

	const char* binary_keyword(const char* p) {
		kind k;
		const char* label = "AND";
		const char* r = p;
		if (keyword(p, "AND")) {
			k = kind::and_;
			r = p + 3;
			const char* s = r;
			while (s < e_ && (*s == ' ' || (*s >= '\t' && *s <= '\r'))) { ++s; }
			if (s > r && keyword(s, "NOT")) {
				k = kind::and_not;
				label = "NOT";
				r = s + 3;
			}
		} else if (keyword(p, "OR")) {
			k = kind::or_;
			label = "OR";
			r = p + 2;
		} else if (keyword(p, "NOT")) {
			k = kind::and_not;
			label = "NOT";
			r = p + 3;
		} else if (keyword(p, "XOR")) {
			k = kind::xor_;
			label = "XOR";
			r = p + 3;
		} else if (keyword(p, "NEAR") || keyword(p, "ADJ")) {
			// A proximity operator not between two words.
			fail(off(p), std::string("Syntax: <expression> ") + (p[0] == 'N' ? "NEAR" : "ADJ") + " <expression>");
			return e_;
		} else {
			return nullptr;
		}
		if (expect_) {
			fail_op(label, off(p));
			return e_;
		}
		while (ops_.size() > frames_.back().ops_base && prec(ops_.back().k) >= prec(k)) { reduce(); }
		ops_.push_back({k, off(p), label});
		expect_ = true;
		return r;
	}

	// Collects the word at p (a word character) and returns where it ends.
	const char* take_word(const char* p, const char* end, raw_word& w) {
		const char* q = p;
		const char32_t c0 = unicode::next_utf8(q, end);
		w.capital = unicode::category_of(c0) == unicode::category::uppercase_letter || unicode::category_of(c0) == unicode::category::titlecase_letter;
		const char* r = scanner_.scan(p, end);
		w.text = scanner_.text();
		return r;
	}

	// A word, with the words that phrase generators join to it ("a-b", "title:x").
	const char* word_group(const char* p) {
		std::vector<raw_word> words;
		std::string prefix;
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
		while (phrase() && r + 1 < e_ && is_generator(*r) && word_at(r + 1)) {
			raw_word nw;
			r = take_word(r + 1, e_, nw);
			words.push_back(std::move(nw));
			generated = true;
		}
		if (r < e_ && *r == '*') { words.back().star = true; }
		// NEAR and ADJ chains.
		kind prox = kind::term;
		std::uint32_t distance = 10;
		bool proximity = false;
		while (boolean()) {
			const char* s = r;
			while (s < e_ && (*s == ' ' || (*s >= '\t' && *s <= '\r'))) { ++s; }
			if (s == r) { break; }
			kind k;
			if (keyword(s, "NEAR") || (s + 4 < e_ && std::string_view(s, 4) == "NEAR" && s[4] == '/')) {
				k = kind::near;
			} else if (keyword(s, "ADJ") || (s + 3 < e_ && std::string_view(s, 3) == "ADJ" && s[3] == '/')) {
				k = kind::phrase;
			} else {
				break;
			}
			const char* t = s + (k == kind::near ? 4 : 3);
			std::uint32_t n = 10;
			bool explicit_n = false;
			if (t < e_ && *t == '/') {
				const char* u = t + 1;
				std::uint64_t v = 0;
				while (u < e_ && *u >= '0' && *u <= '9') {
					v = v * 10 + std::uint64_t(*u - '0');
					if (v > 0xFFFFFFFFu) { v = 0xFFFFFFFFu; }
					++u;
				}
				if (u == t + 1 || word_at(u)) { break; }
				n = std::uint32_t(v);
				explicit_n = true;
				t = u;
			} else if (word_at(t)) {
				break;
			}
			const char* x = t;
			while (x < e_ && (*x == ' ' || (*x >= '\t' && *x <= '\r'))) { ++x; }
			if (x == t && !explicit_n) { break; }
			if (x >= e_ || !word_at(x) || (prox != kind::term && prox != k)) {
				fail_op(k == kind::near ? "NEAR" : "ADJ", off(s));
				return e_;
			}
			if (prox == kind::term) {
				prox = k;
				distance = n;
			}
			proximity = true;
			raw_word nw;
			r = take_word(x, e_, nw);
			words.push_back(std::move(nw));
			while (phrase() && r + 1 < e_ && is_generator(*r) && word_at(r + 1)) {
				raw_word gw;
				r = take_word(r + 1, e_, gw);
				words.push_back(std::move(gw));
			}
		}
		if (proximity) {
			operand(finish(words, prefix, prox, true, distance), off(p));
		} else {
			operand(finish(words, prefix, generated ? kind::phrase : kind::term, false, 0), off(p));
		}
		return r;
	}

	const char* quoted(const char* p, std::string prefix = {}) {
		const char* s = p + 1;
		const char* q = s;
		while (q < e_ && *q != '"') { ++q; }
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
		operand(finish(words, prefix, kind::phrase, false, 0), off(p));
		return q < e_ ? q + 1 : q;
	}

	// Terms for words: a single term, or a phrase or proximity operator over them. Returns
	// no_node when every word was dropped. A proximity window is distance + terms - 1.
	node_id finish(const std::vector<raw_word>& words, const std::string& prefix, kind k, bool proximity, std::uint32_t distance) {
		std::vector<node_id> terms;
		terms.reserve(words.size());
		for (const auto& w : words) {
			if (w.text.size() <= cfg_.max_word_length) { terms.push_back(make_term(w, prefix, k != kind::term)); }
		}
		if (terms.empty()) { return no_node; }
		if (terms.size() == 1) { return terms[0]; }
		const std::uint32_t count = std::uint32_t(terms.size());
		const std::uint64_t window = proximity ? std::uint64_t(distance) + count - 1 : count;
		const node_id n = bld_.op(k, std::uint32_t(window > 0xFFFFFFFFu ? 0xFFFFFFFFu : window));
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

	// Pushes an operand; n == no_node drops it.
	void operand(node_id n, std::size_t offset) {
		const mark m = std::exchange(pending_, mark::none);
		if (n == no_node) { return; }
		push_operand(n, m, offset);
	}

	void push_operand(node_id n, mark m, std::size_t offset) {
		if (!expect_) { reduce_all(); }
		vals_.push_back({n, m, offset});
		expect_ = false;
	}

	const char* open_frame(const char* p) {
		frames_.push_back({ops_.size(), vals_.size(), expect_, std::exchange(pending_, mark::none), off(p)});
		expect_ = true;
		return p + 1;
	}

	void close_frame() {
		const frame f = frames_.back();
		const node_id n = close_group();
		if (failed_) { return; }
		frames_.pop_back();
		expect_ = f.parent_expect;
		if (n != no_node) { push_operand(n, f.m, f.offset); }
	}

	// Reduces the current frame's operators and combines its items into one node.
	node_id close_group() {
		const frame f = frames_.back();
		if (expect_ && ops_.size() > f.ops_base) {
			fail_op(ops_.back().label, ops_.back().offset);
			return no_node;
		}
		reduce_all();
		node_id res = no_node;
		const std::size_t first = f.vals_base;
		if (vals_.size() > first) { res = combine_items(first); }
		vals_.resize(first);
		return res;
	}

	// Required (+) items, optional items, then excluded (-) items.
	node_id combine_items(std::size_t first) {
		if (vals_.size() - first == 1 && vals_[first].m == mark::none) { return vals_[first].n; }
		node_id req = no_node, opt = no_node, exc = no_node;
		auto join = [&](node_id acc, kind k, node_id n) { return acc == no_node ? n : bld_.combine(k, acc, n); };
		const bool all_required = cfg_.default_op == kind::and_;
		for (std::size_t i = first; i < vals_.size(); ++i) {
			const item& it = vals_[i];
			if (it.m == mark::plus || (it.m == mark::none && all_required)) {
				req = join(req, kind::and_, it.n);
			} else if (it.m == mark::minus) {
				exc = join(exc, kind::or_, it.n);
			} else {
				opt = join(opt, cfg_.default_op, it.n);
			}
		}
		node_id res = req;
		if (res == no_node) {
			res = opt;
		} else if (opt != no_node) {
			res = bld_.combine(kind::and_maybe, res, opt);
		}
		if (exc != no_node) {
			if (res == no_node) {
				fail_op("NOT", vals_[first].offset);
				return no_node;
			}
			res = bld_.combine(kind::and_not, res, exc);
		}
		return res;
	}

	void reduce() {
		const pending_op o = ops_.back();
		ops_.pop_back();
		const item r = vals_.back();
		vals_.pop_back();
		item& l = vals_.back();
		l.n = bld_.combine(o.k, l.n, r.n);
	}

	void reduce_all() {
		while (ops_.size() > frames_.back().ops_base) { reduce(); }
	}

	const config& cfg_;
	const char* b_;
	const char* e_;
	parse_result& out_;
	builder bld_;
	analysis::word_scanner scanner_;
	std::vector<item> vals_;
	std::vector<pending_op> ops_;
	std::vector<frame> frames_;
	bool expect_ = true;
	bool failed_ = false;
	mark pending_ = mark::none;
	std::uint32_t termpos_ = 0;
};

}  // namespace

parse_result parse(std::string_view text, const config& cfg) {
	parse_result res;
	if (text.size() > max_query_bytes) {
		res.err = error{error_kind::too_long, 0, "Query too long"};
		return res;
	}
	parser(text, cfg, res).run();
	return res;
}

}  // namespace queryparser
