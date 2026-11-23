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

// parse(): query text to a tree of terms and operators, through the node builder and the iterative parser.

#include "queryparser.hh"

#include "queryparser/parser.hh"

#include <new>

namespace queryparser {

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
			plain.flags |= plain_quotes;
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
