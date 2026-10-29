// The shape every parse result has: from the root, each node is reached once (so child lists
// are acyclic and a node has one parent), links stay inside the tree, and a node's last child
// ends its list. Never loops, whatever the links say.
#pragma once

#include "queryparser.hh"

#include <vector>

namespace queryparser {

inline bool well_formed(const query& q) {
	if (q.empty()) { return true; }
	const std::size_t n = q.size();
	if (q.root() >= n || q.at(q.root()).next != no_node) { return false; }
	std::vector<bool> seen(n, false);
	std::vector<node_id> stack{q.root()};
	seen[q.root()] = true;
	while (!stack.empty()) {
		const node_id id = stack.back();
		stack.pop_back();
		const node& p = q.at(id);
		node_id last = no_node;
		for (node_id c = p.first; c != no_node; c = q.at(c).next) {
			if (c >= n || seen[c]) { return false; }
			seen[c] = true;
			last = c;
			stack.push_back(c);
		}
		if (last != p.last) { return false; }
	}
	return true;
}

}  // namespace queryparser
