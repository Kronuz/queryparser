// A reader for the oracle's JSON lines: a flat object whose values are strings, numbers, arrays or objects.
// Strings keep their decoded UTF-8; any other value is kept as its raw text.
#pragma once

#include "unicode.hh"

#include <cstdlib>
#include <map>
#include <string>

namespace jsonl {

inline std::map<std::string, std::string> row(const std::string& line) {
	std::map<std::string, std::string> m;
	const char* p = line.c_str();
	auto str = [&] {
		std::string s;
		for (++p; *p != '"'; ++p) {
			if (*p != '\\') {
				s += *p;
				continue;
			}
			switch (*++p) {
				case 'n': s += '\n'; break;
				case 't': s += '\t'; break;
				case 'r': s += '\r'; break;
				case 'b': s += '\b'; break;
				case 'f': s += '\f'; break;
				case 'u': {
					char32_t c = char32_t(std::strtoul(std::string(p + 1, 4).c_str(), nullptr, 16));
					p += 4;
					if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
						const char32_t lo = char32_t(std::strtoul(std::string(p + 3, 4).c_str(), nullptr, 16));
						c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
						p += 6;
					}
					unicode::append_utf8(s, c);
					break;
				}
				default: s += *p;
			}
		}
		++p;
		return s;
	};
	while (*p && *p != '}') {
		if (*p != '"') {
			++p;
			continue;
		}
		std::string k = str();
		while (*p == ':' || *p == ' ') { ++p; }
		if (*p == '"') {
			m[k] = str();
		} else if (*p == '[' || *p == '{') {
			const char open = *p, close = open == '[' ? ']' : '}';
			const char* s = p;
			for (bool in = false; *p && (in || *p != close); ++p) {
				if (*p == '\\' && in) { ++p; }
				else if (*p == '"') { in = !in; }
			}
			m[k] = std::string(s, ++p);
		} else {
			const char* s = p;
			while (*p && *p != ',' && *p != '}') { ++p; }
			m[k] = std::string(s, p);
		}
	}
	return m;
}

}  // namespace jsonl
