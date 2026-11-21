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

// query::describe(): a parsed tree as the text of Xapian's Query::get_description().

#include "queryparser.hh"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

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

}  // namespace queryparser
