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

// The parser's grouping and tree: items and frames, brackets, the combine pass over a group's
// items, joins, folds and operator reduction. Included by parser.hh.

#pragma once

#include "queryparser/parser.hh"

namespace queryparser {

inline void parser::operand(node_id n, std::size_t offset, const char* end, bool plain, bool stop) {
	const mark m = std::exchange(pending_, mark::none);
	const bool glued = std::exchange(glued_, false);
	if (n == no_node && !std::exchange(stopped_, false)) { return; }
	push_item({n, m, offset, std::size_t(end - b_), plain, stop, anchor_, solo_, kw_, quote_, false, std::exchange(alt_, no_node), glued});
	anchor_ = solo_ = kw_ = quote_ = false;
}

inline void parser::push_item(item it) {
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
				// A sign apart from the operator makes the text plain words.
				for (std::size_t i = o.end; i + 1 < it.offset; ++i) {
					if (!is_space(b_[i])) {
						reparse();
						return;
					}
				}
				o.k = kind::and_not;
			}
			it.m = mark::none;
		}
		expect_ = false;
	}
	items_.push_back(it);
}

inline const char* parser::open_frame(const char* p) {
	frames_.push_back({ops_.size(), vals_.size(), items_.size(), expect_, std::exchange(pending_, mark::none), off(p)});
	expect_ = true;
	return p + 1;
}

inline void parser::close_frame(const char* end) {
	const frame f = frames_.back();
	const bool had = !expect_ || vals_.size() > f.vals_base || ops_.size() > f.ops_base;
	const node_id n = close_group();
	if (failed_) { return; }
	frames_.pop_back();
	expect_ = f.parent_expect;
	if (had) { push_item({n, f.m, f.offset, off(end), false, std::exchange(lone_stop_, false), false, false, false, false, true}); }
}

inline node_id parser::close_group() {
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

inline node_id parser::close_group_items() {
	const std::size_t first = top().items_base;
	const node_id res = combine_items(first);
	items_.resize(first);
	return res;
}

inline node_id parser::join(kind k, node_id l, node_id r) {
	if (l == no_node) { return k == kind::and_not ? no_node : r; }
	if (r == no_node) { return l; }
	switch (k) {
		case kind::near: case kind::phrase: case kind::elite_set: case kind::synonym: case kind::max: return nary(k, {l, r});
		default: return bld_.combine(k, l, r);
	}
}

inline node_id parser::nary(kind k, const std::vector<node_id>& kids) {
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

inline node_id parser::join_default(const std::vector<node_id>& kids) {
	if (kids.empty()) { return no_node; }
	node_id acc = kids[0];
	for (std::size_t i = 1; i < kids.size(); ++i) { acc = join(default_op_, acc, kids[i]); }
	return acc;
}

inline node_id parser::join_run(const std::vector<node_id>& kids) {
	switch (default_op_) {
		case kind::near: case kind::phrase: case kind::elite_set: case kind::synonym: case kind::max: return nary(default_op_, kids);
		default: return join_default(kids);
	}
}

inline node_id parser::combine_items(std::size_t first) {
	if (items_.size() - first == 1 && items_[first].m == mark::minus && items_[first].quote && ((cfg_.stopper || !boolean()) || bld_.at(items_[first].n).k == kind::term)) {
		reparse();
		return no_node;
	}
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

inline node_id parser::combine_pass(std::size_t first, bool demote, bool& retry) {
	const std::size_t end = items_.size();
	if (demote) {
		for (std::size_t i = first; i < end; ++i) {
			item& x = items_[i];
			if (x.m != mark::minus) { continue; }
			x.m = mark::none;
			// An excluded phrase that reads as unmarked is read as plain words.
			if (x.quote) {
				reparse();
				return no_node;
			}
			if (x.group && x.stop) { x.n = no_node; }
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
	for (std::size_t i = first; i < end;) {
		std::size_t j = i + 1;
		if ((items_[i].plain || items_[i].solo) && items_[i].m == mark::none) {
			// A run extends over plain words and ends with a word that stands alone.
			while (!items_[i].solo && j < end && (items_[j].plain || items_[j].solo) && !items_[j].glued && items_[j].m == mark::none &&
			       gap_is_space(items_[j - 1], items_[j].offset)) {
				++j;
				if (items_[j - 1].solo) { break; }
			}
			bool nonstop = false, anchored = false;
			for (std::size_t x = i; x < j; ++x) {
				nonstop |= !items_[x].stop;
				anchored |= items_[x].anchor;
			}
			anchored |= j < end && items_[j].anchor && items_[j].m == mark::none;
			if (nonstop || j - i < 2 || anchored) {
				for (std::size_t x = i; x < j; ++x) {
					if (items_[x].stop) { items_[x].n = no_node; }
				}
			}
		}
		run_end[i - first] = j;
		i = j;
	}
	// Collect the elements first: building a node extends its left operand in place, so a pass
	// that may ask for a retry builds nothing until it knows it will not.
	std::vector<node_id> req, opt, exc;
	struct element { bool plus; std::vector<node_id> kids; };  // a + item, or an unmarked run
	std::vector<element> elements;
	const bool all_required = default_op_ == kind::and_;
	if (cfg_.stopper && !demote) {
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
			if (x.n != no_node) { elements.push_back({true, {x.n}}); }
		} else if (x.m == mark::minus) {
			if (x.n != no_node) { exc.push_back(x.n); }
		} else {
			std::vector<node_id> kids;
			for (std::size_t y = i; y < j; ++y) {
				if (items_[y].n != no_node) { kids.push_back(items_[y].n); }
			}
			if (!kids.empty()) { elements.push_back({false, std::move(kids)}); }
		}
		i = j;
	}
	if (!exc.empty() && elements.empty() && !has(flag::pure_not) && (cfg_.stopper || !boolean()) && !demote) {
		retry = true;
		return no_node;
	}
	for (const element& e : elements) {
		if (e.plus) {
			req.push_back(e.kids[0]);
			continue;
		}
		const node_id el = e.kids.size() == 1 ? e.kids[0] : (all_required ? fold(e.kids, kind::and_) : join_run(e.kids));
		if (failed_) { return no_node; }
		if (el != no_node) { (all_required ? req : opt).push_back(el); }
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
		if (res == no_node) {
			fail_op("NOT", items_[first].offset);
			return no_node;
		}
		res = bld_.combine(kind::and_not, res, x);
	}
	return res;
}

inline node_id parser::fold(const std::vector<node_id>& v, kind k) {
	node_id acc = no_node;
	for (node_id n : v) { acc = acc == no_node ? n : bld_.combine(k, acc, n); }
	return acc;
}

inline bool parser::gap_is_generators(std::size_t from, std::size_t to) const {
	for (; from < to; ++from) {
		if (!is_generator(b_[from])) { return false; }
	}
	return true;
}

inline bool parser::gap_is_space(const item& prev, std::size_t to) const {
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

inline void parser::reduce() {
	const pending_op o = ops_.back();
	ops_.pop_back();
	const node_id r = vals_.back();
	vals_.pop_back();
	node_id& l = vals_.back();
	l = join(o.k, l, r);
}

}  // namespace queryparser
