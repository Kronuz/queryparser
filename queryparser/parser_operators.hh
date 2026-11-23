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

// The parser's driver and operator recognition: the constructor, run() and step()'s dispatch,
// keywords, AND/OR/NOT/XOR, NEAR/ADJ and stray brackets. Included by parser.hh.

#pragma once

#include "queryparser/parser.hh"

namespace queryparser {

inline parser::parser(std::string_view text, const config& cfg, parse_result& out)
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

inline void parser::run() {
	frames_.push_back({0, 0, 0, true, mark::none, 0});
	const char* p = b_;
	while (p < e_ && !failed_) { p = step(p); }
	while (frames_.size() > 1 && !failed_) { close_frame(e_); }
	if (failed_) { return; }
	const node_id root = close_group();
	if (!failed_ && root != no_node) { bld_.set_root(root); }
}

inline bool parser::word_at(const char* p) const {
	if (p >= e_) { return false; }
	return analysis::is_word_char(unicode::next_utf8(p, e_));
}

inline void parser::fail(std::size_t offset, std::string message, error_kind k) {
	failed_ = true;
	out_.err = error{k, offset, std::move(message)};
}

inline void parser::fail_op(const char* label, std::size_t offset) { fail(offset, std::string("Syntax: <expression> ") + label + " <expression>"); }

inline bool parser::keyword(const char* p, std::string_view w, bool any_case) const {
	if (std::size_t(e_ - p) < w.size()) { return false; }
	for (std::size_t i = 0; i < w.size(); ++i) {
		const char c = any_case && p[i] >= 'a' && p[i] <= 'z' ? char(p[i] - 'a' + 'A') : p[i];
		if (c != w[i]) { return false; }
	}
	const char* q = p + w.size();
	std::uint32_t d;
	const char* x;
	if (has(flag::cjk_ngram) && q < e_) {
		const char* z = q;
		if (analysis::is_ngram_char(unicode::next_utf8(z, e_))) { return true; }
	}
	return !word_at(q) && !((has(flag::wildcard) || glob_) && q < e_ && *q == '*') && !fuzzy_at(q, d, x);
}

inline bool parser::op_word(const char* p, std::string_view w) const { return keyword(p, w, has(flag::boolean_any_case)); }

inline bool parser::op_keyword(const char* p) const { return op_word(p, "AND") || op_word(p, "OR") || op_word(p, "NOT") || op_word(p, "XOR"); }

inline bool parser::proximity_word(const char* p) const { return keyword(p, "NEAR") || keyword(p, "ADJ") || slash_word(p, "NEAR") || slash_word(p, "ADJ"); }

inline bool parser::slash_word(const char* p, std::string_view w) const {
	return std::size_t(e_ - p) > w.size() && std::string_view(p, w.size()) == w && p[w.size()] == '/';
}

inline const char* parser::step(const char* p) {
	const char c = *p;
	if (c == '"' && phrase() && !has(plain_quotes)) { return quoted(p); }
	if (c == '(' && (boolean() || pending_ != mark::none)) { return open_frame(p); }
	if (c == ')' && (boolean() || frames_.size() > 1)) {
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

inline const char* parser::binary_keyword(const char* p) {
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
		// An excluded item before an operator word reads the text as plain words.
		if (items_.size() > top().items_base && items_.back().m == mark::minus) { return reparse(); }
		const node_id g = close_group_items();
		if (failed_) { return e_; }
		vals_.push_back(g);
	}
	while (ops_.size() > top().ops_base && prec(ops_.back().k) >= prec(k)) {
		reduce();
		if (failed_) { return e_; }
	}
	ops_.push_back({k, off(p), label, false, off(r)});
	expect_ = true;
	return r;
}

inline bool parser::quote_before(const char* p) const {
	if (!phrase() || expect_ || items_.size() <= top().items_base) { return false; }
	const item& last = items_.back();
	return last.quote && gap_is_space(last, off(p));
}

inline bool parser::prox_rereads(const char* p) const {
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

inline const char* parser::following_operator(const char* r) const {
	while (r < e_ && is_space(*r)) { ++r; }
	return r < e_ && op_keyword(r) ? r : nullptr;
}

inline kind parser::op_kind(const char* p) const {
	if (op_word(p, "OR")) { return kind::or_; }
	if (op_word(p, "XOR")) { return kind::xor_; }
	return kind::and_;  // AND, AND NOT and NOT bind alike
}

inline const char* parser::op_label(const char* p) const {
	if (op_word(p, "OR")) { return "OR"; }
	if (op_word(p, "XOR")) { return "XOR"; }
	if (op_word(p, "NOT")) { return "NOT"; }
	const char* s = p + 3;
	while (s < e_ && is_space(*s)) { ++s; }
	return s > p + 3 && op_word(s, "NOT") ? "AND NOT" : "AND";
}

inline void parser::stray_ket() {
	if (expect_ && ops_.size() > top().ops_base && !ops_.back().split) {
		fail_op(ops_.back().label, ops_.back().offset);
		return;
	}
	reparse();
}

}  // namespace queryparser
