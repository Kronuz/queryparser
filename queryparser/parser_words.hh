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

// The parser's word layer: globs, fuzzy suffixes, word groups, CJK n-grams, quoted phrases, stop
// words and terms. Included by parser.hh.

#pragma once

#include "queryparser/parser.hh"

namespace queryparser {

inline const char* parser::take_word(const char* p, const char* end, raw_word& w) {
	const char* q = p;
	const char32_t c0 = unicode::next_utf8(q, end);
	w.capital = unicode::category_of(c0) == unicode::category::uppercase_letter || unicode::category_of(c0) == unicode::category::titlecase_letter;
	const char* r = scanner_.scan(p, end);
	w.text = scanner_.text();
	return r;
}

inline const char* parser::glob_word(const char* p) {
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

inline bool parser::chain_follows(const char* r) const {
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

inline bool parser::glob_follows(const char* r) const {
	if (!glob_) { return false; }
	while (r < e_) {
		if (*r == '?' || *r == '*') { return true; }
		const char* q = r;
		if (!analysis::is_word_char(unicode::next_utf8(q, e_))) { return false; }
		r = q;
	}
	return false;
}

inline bool parser::fuzzy_at(const char* r, std::uint32_t& dist, const char*& end) const {
	if (!has(flag::fuzzy) || r >= e_ || *r != '~') { return false; }
	const char* j = r + 1;
	const char* d = j;
	std::uint64_t v = 0;
	while (d < e_ && *d >= '0' && *d <= '9') {
		v = v * 10 + std::uint64_t(*d - '0');
		if (v > 0xFFFFFFFFu) { v = 0xFFFFFFFFu; }
		++d;
	}
	if (d == j && !word_at(j) && !(j < e_ && (*j == '~' || *j == '-'))) {
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

inline const char* parser::joins(const char* g, mark m) const {
	const char* w = g;
	while (w < e_ && is_generator(*w)) { ++w; }
	if (!phrase() || w == g || !word_at(w) || glued_anchor(g, m)) { return nullptr; }
	return w;
}

inline bool parser::glued_anchor(const char* g, mark m) const {
	const char* w = g;
	while (w < e_ && is_generator(*w)) { ++w; }
	if (!phrase() || w == g || !word_at(w) || m == mark::minus) { return false; }
	raw_word probe;
	const char* u = const_cast<parser*>(this)->take_word(w, e_, probe);
	std::uint32_t d;
	const char* x;
	return (has(flag::wildcard) && u < e_ && *u == '*') || glob_follows(w) || fuzzy_at(u, d, x);
}

inline const char* parser::word_group(const char* p) {
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

inline bool parser::juxtaposed_word(const char* r) {
	const char* s = r;
	while (s < e_ && is_space(*s)) { ++s; }
	if (s == r || s >= e_ || !word_at(s) || op_keyword(s) || proximity_word(s)) { return false; }
	raw_word w;
	const char* u = take_word(s, e_, w);
	return !(has(flag::wildcard) && u < e_ && *u == '*') && !glob_follows(s) && !fuzzy_at(u, dist_, tmp_);
}

inline bool parser::expands(const char* r, const raw_word& w) const {
	std::uint32_t d;
	const char* x;
	if (has(flag::wildcard) && r < e_ && *r == '*') { return true; }
	if (fuzzy_at(r, d, x)) { return true; }
	if (glob_follows(r)) { return true; }
	return has(flag::partial) && r == e_ && code_points(w.text) >= 2;
}

inline std::size_t parser::code_points(const std::string& s) {
	std::size_t n = 0;
	for (unsigned char c : s) { n += (c & 0xC0) != 0x80; }
	return n;
}

inline const char* parser::reparse() {
	reparse_ = true;
	failed_ = true;
	return e_;
}

inline const char* parser::single(const raw_word& w, const std::string& prefix, const char* r, const char* start) {
	// An excluded word expands to a wildcard; if the exclusion reads as unmarked, the plain term stands.
	const bool minus = pending_ == mark::minus;
	if (has(flag::wildcard) && r < e_ && *r == '*') {
		anchor_ = true;
		if (minus) {
			alt_ = make_term(w, prefix, false);
		} else {
			++termpos_;
		}
		operand(bld_.leaf(kind::wildcard, prefix + w.text, 0, cfg_.wildcard_expansion), off(start), r + 1, false, minus && stopword(w));
		return r + 1;
	}
	std::uint32_t dist;
	const char* fend;
	if (!minus && fuzzy_at(r, dist, fend)) {
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
		bld_.add(pair, bld_.copy_leaf(t));  // t itself stands for an exclusion that reads as unmarked
		operand(pair, off(start), r, false);
		return r;
	}
	std::vector<raw_word> one{w};
	const bool solo = r + 1 < e_ && ((has(flag::fuzzy) && *r == '~') || *r == '*');
	const bool stop = stopword(w);
	solo_ = solo;
	kw_ = w.capital && (w.text == "and" || w.text == "or" || w.text == "not" || w.text == "xor" || w.text == "near" || w.text == "adj");
	glued_ = glued_anchor(r, pending_);
	operand(finish(one, prefix, kind::term, false, 0), off(start), r, !solo, stop);
	return r;
}

inline const char* parser::ngram_run(const char* p) {
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

inline const char* parser::quoted(const char* p, std::string prefix) {
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
	if (q >= e_ && (glob_ || has(flag::wildcard)) && std::string_view(s, std::size_t(q - s)).find_first_of("*?") != std::string_view::npos) { return reparse(); }
	std::vector<raw_word> words;
	const char* t = s;
	while (t < q) {
		const char* u = t;
		const char32_t c = unicode::next_utf8(u, q);
		if (has(flag::cjk_ngram) && analysis::is_word_char(c) && analysis::is_ngram_char(c)) {
			// A CJK run is its units at one position.
			bool first = true;
			while (t < q) {
				const char* v = t;
				const char32_t d = unicode::next_utf8(v, q);
				if (!analysis::is_word_char(d) || !analysis::is_ngram_char(d)) { break; }
				raw_word w;
				w.capital = false;
				unicode::append_utf8(w.text, unicode::tolower(d));
				w.same_pos = !first;
				first = false;
				words.push_back(std::move(w));
				t = v;
			}
		} else if (analysis::is_word_char(c)) {
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

inline bool parser::stop_all() const { return cfg_.stopper && cfg_.stop == analysis::stop_strategy::all; }

inline bool parser::is_stop(const raw_word& w) const { return cfg_.stopper && (*cfg_.stopper)(w.text); }

inline bool parser::stopword(const raw_word& w) const { return cfg_.stopper && !stop_all() && is_stop(w); }

inline node_id parser::finish(const std::vector<raw_word>& words, const std::string& prefix, kind k, bool proximity, std::uint32_t distance) {
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

inline node_id parser::make_term(const raw_word& w, const std::string& prefix, bool in_phrase) {
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
		} else if (s == stem_strategy::some && !in_phrase && !w.star) {
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
	return bld_.term(t, w.same_pos ? termpos_ : ++termpos_);
}

}  // namespace queryparser
