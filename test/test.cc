// Grammar tests beyond the oracle vectors. Where Xapian's behaviour for a shape is not in
// the vectors, the expected text records what this parser does (V2 will confirm it).
#include "queryparser.hh"

#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
#define CHECK_EQ(got, want) \
	do { \
		const std::string g_ = (got), w_ = (want); \
		if (g_ != w_) { std::fprintf(stderr, "%s:%d: got  %s\n%*s want %s\n", __FILE__, __LINE__, g_.c_str(), 0, "", w_.c_str()); ++failures; } \
	} while (0)

static snowball::stemmer english("english");

namespace {

using namespace queryparser;

std::string run(std::string_view text, const config& cfg) {
	const auto r = parse(text, cfg);
	return r.err ? "ERROR " + r.err->message : r.tree.describe();
}

config plain() {
	config c;
	c.stem = analysis::stem_strategy::none;
	c.stemmer = &english;
	return c;
}

std::string P(std::string_view text) { return run(text, plain()); }

}  // namespace

int main() {
	using namespace queryparser;
	// Operators and precedence.
	CHECK_EQ(P("a b"), "Query((a@1 OR b@2))");
	CHECK_EQ(P("a AND b AND c"), "Query((a@1 AND b@2 AND c@3))");
	CHECK_EQ(P("a OR b AND c"), "Query((a@1 OR (b@2 AND c@3)))");
	CHECK_EQ(P("a AND b OR c"), "Query(((a@1 AND b@2) OR c@3))");
	CHECK_EQ(P("a XOR b OR c"), "Query(((a@1 XOR b@2) OR c@3))");
	CHECK_EQ(P("a OR b XOR c"), "Query((a@1 OR (b@2 XOR c@3)))");
	CHECK_EQ(P("a XOR b AND c"), "Query((a@1 XOR (b@2 AND c@3)))");
	CHECK_EQ(P("a AND b XOR c"), "Query(((a@1 AND b@2) XOR c@3))");
	CHECK_EQ(P("a AND NOT b"), "Query((a@1 AND_NOT b@2))");
	CHECK_EQ(P("a NOT b NOT c"), "Query(((a@1 AND_NOT b@2) AND_NOT c@3))");
	CHECK_EQ(P("a AND (b OR c)"), "Query((a@1 AND (b@2 OR c@3)))");
	CHECK_EQ(P("(a)"), "Query(a@1)");
	CHECK_EQ(P("((a b))"), "Query((a@1 OR b@2))");
	CHECK_EQ(P("()"), "Query()");
	CHECK_EQ(P(""), "Query()");
	CHECK_EQ(P("a (b c)"), "Query((a@1 OR b@2 OR c@3))");
	// Required, optional and excluded items.
	CHECK_EQ(P("+a b"), "Query((a@1 AND_MAYBE b@2))");
	CHECK_EQ(P("+a -b c"), "Query(((a@1 AND_MAYBE c@3) AND_NOT b@2))");
	CHECK_EQ(P("+(a b) c"), "Query(((a@1 OR b@2) AND_MAYBE c@3))");
	CHECK_EQ(P("-\"a b\" c"), "Query((c@3 AND_NOT (a@1 PHRASE 2 b@2)))");
	CHECK_EQ(P("a-b"), "Query((a@1 PHRASE 2 b@2))");
	CHECK_EQ(P("a - b"), "Query((a@1 OR b@2))");
	CHECK_EQ(P("a+b"), "Query((a@1 OR b@2))");
	// Phrases and proximity.
	CHECK_EQ(P("\"a b c\""), "Query((a@1 PHRASE 3 b@2 PHRASE 3 c@3))");
	CHECK_EQ(P("\"a\""), "Query(a@1)");
	CHECK_EQ(P("\"a b"), "Query((a@1 PHRASE 2 b@2))");
	CHECK_EQ(P("\"\""), "Query()");
	CHECK_EQ(P("a NEAR/3 b"), "Query((a@1 NEAR 4 b@2))");
	CHECK_EQ(P("a ADJ/2 b"), "Query((a@1 PHRASE 3 b@2))");
	CHECK_EQ(P("a NEAR b NEAR c"), "Query((a@1 NEAR 12 b@2 NEAR 12 c@3))");
	CHECK_EQ(P("a NEAR/0 b"), "Query((a@1 NEAR 1 b@2))");
	CHECK_EQ(P("a NEAR/4294967295 b"), "Query((a@1 NEAR 4294967295 b@2))");
	CHECK_EQ(P("x a NEAR b"), "Query((x@1 OR (a@2 NEAR 11 b@3)))");
	CHECK_EQ(P("a-b NEAR c"), "Query((a@1 NEAR 12 b@2 NEAR 12 c@3))");
	// Errors.
	CHECK_EQ(P("a AND"), "ERROR Syntax: <expression> AND <expression>");
	CHECK_EQ(P("AND a"), "ERROR Syntax: <expression> AND <expression>");
	CHECK_EQ(P("a OR OR b"), "ERROR Syntax: <expression> OR <expression>");
	CHECK_EQ(P("-a"), "ERROR Syntax: <expression> NOT <expression>");
	CHECK_EQ(P("a NEAR"), "ERROR Syntax: <expression> NEAR <expression>");
	CHECK_EQ(P("NEAR a"), "ERROR Syntax: <expression> NEAR <expression>");
	CHECK_EQ(P("a NEAR b ADJ c"), "ERROR Syntax: <expression> ADJ <expression>");
	CHECK_EQ(P("(a AND)"), "ERROR Syntax: <expression> AND <expression>");
	{
		const auto r = parse("ab NOT", plain());
		CHECK_EQ(r.err ? std::to_string(r.err->offset) : "none", "3");
	}
	// Unbalanced brackets.
	CHECK_EQ(P("(a b"), "Query((a@1 OR b@2))");
	CHECK_EQ(P("a) b"), "Query((a@1 OR b@2))");
	// Flags off: the syntax is plain separators and words.
	{
		config c = plain();
		c.flags = 0;
		CHECK_EQ(run("a AND (b) \"c d\" +e -f-g", c), "Query((a@1 OR and@2 OR b@3 OR c@4 OR d@5 OR e@6 OR f@7 OR g@8))");
		c.flags = flag::boolean;
		CHECK_EQ(run("a AND b -c", c), "Query(((a@1 AND b@2) OR c@3))");
		c.flags = flag::phrase;
		CHECK_EQ(run("\"a b\" c-d AND", c), "Query(((a@1 PHRASE 2 b@2) OR (c@3 PHRASE 2 d@4) OR and@5))");
		c.flags = flag::lovehate;
		CHECK_EQ(run("+a -b", c), "Query((a@1 AND_NOT b@2))");
	}
	// Default operator.
	{
		config c = plain();
		c.default_op = kind::and_;
		CHECK_EQ(run("a b", c), "Query((a@1 AND b@2))");
		CHECK_EQ(run("a b -c", c), "Query(((a@1 AND b@2) AND_NOT c@3))");
		CHECK_EQ(run("a OR b c", c), "Query(((a@1 OR b@2) AND c@3))");
		c.default_op = kind::xor_;
		CHECK_EQ(run("a b", c), "Query((a@1 XOR b@2))");
	}
	// Stemming strategies.
	{
		config c = plain();
		c.stem = analysis::stem_strategy::some;
		CHECK_EQ(run("Running runs 7up", c), "Query((running@1 OR Zrun@2 OR 7up@3))");
		CHECK_EQ(run("\"runs runs\" runs-runs", c), "Query(((runs@1 PHRASE 2 runs@2) OR (runs@3 PHRASE 2 runs@4)))");
		c.stem = analysis::stem_strategy::all_z;
		CHECK_EQ(run("Runs", c), "Query(Zrun@1)");
		c.stem = analysis::stem_strategy::all;
		CHECK_EQ(run("\"Runs ran\"", c), "Query((run@1 PHRASE 2 ran@2))");
		c.stemmer = nullptr;
		CHECK_EQ(run("Runs", c), "Query(runs@1)");
	}
	// Words: case folding, acronyms, apostrophes, joined digits, suffixes, long words.
	CHECK_EQ(P("U.S.A. Obama's 3.14 c++ A&B"), "Query((usa@1 OR obama's@2 OR 3.14@3 OR c++@4 OR a&b@5))");
	CHECK_EQ(P(std::string(70, 'a') + " b"), "Query(b@1)");
	CHECK_EQ(P(std::string(70, 'a')), "Query()");
	CHECK_EQ(P("x " + std::string(64, 'a')), "Query((x@1 OR " + std::string(64, 'a') + "@2))");
	CHECK_EQ(P("AND"), "ERROR Syntax: <expression> AND <expression>");
	CHECK_EQ(P("android ORANGE and or"), "Query((android@1 OR orange@2 OR and@3 OR or@4))");
	// Field resolver.
	{
		config c = plain();
		c.resolver = [](std::string_view name, field_spec& out) {
			if (name == "title") { out.prefix = "S"; return true; }
			return false;
		};
		CHECK_EQ(run("title:obama", c), "Query(Sobama@1)");
		CHECK_EQ(run("title:\"a b\" x:y", c), "Query(((Sa@1 PHRASE 2 Sb@2) OR (x@3 PHRASE 2 y@4)))");
		CHECK_EQ(run("title: a", c), "Query(a@1)");
	}
	// Step 2: wildcard, partial, fuzzy, CJK n-grams, pure NOT, any-case operators.
	{
		auto with = [](std::uint32_t extra) {
			config c = plain();
			c.flags = default_flags | extra;
			return c;
		};
		const config w = with(flag::wildcard), pa = with(flag::partial), fz = with(flag::fuzzy);
		CHECK_EQ(run("obam*", w), "Query(WILDCARD SYNONYM obam)");
		CHECK_EQ(run("Obam* foo", w), "Query((WILDCARD SYNONYM obam OR foo@1))");
		CHECK_EQ(run("obam* AND x-y*", w), "Query((WILDCARD SYNONYM obam AND (x@1 PHRASE 2 y@2)))");
		CHECK_EQ(run("obam*", plain()), "Query(obam@1)");
		// Partial: only the last word, at the end of the text, of two or more characters.
		CHECK_EQ(run("foo ba", pa), "Query((foo@1 OR (WILDCARD SYNONYM ba OR ba@2)))");
		CHECK_EQ(run("foo b", pa), "Query((foo@1 OR b@2))");
		CHECK_EQ(run("foo ba ", pa), "Query((foo@1 OR ba@2))");
		CHECK_EQ(run("foo \"ba\"", pa), "Query((foo@1 OR ba@2))");
		CHECK_EQ(run("foo b-a", pa), "Query((foo@1 OR (b@2 PHRASE 2 a@3)))");
		CHECK_EQ(run("foo OR bar OR baz", pa), "Query((foo@1 OR bar@2 OR (WILDCARD SYNONYM baz OR baz@3)))");
		CHECK_EQ(run("é", pa), "Query(é@1)");
		CHECK_EQ(run("éa", pa), "Query((WILDCARD SYNONYM éa OR éa@1))");
		// NEAR or ADJ next to an expanding word reads the text as plain words.
		CHECK_EQ(run("foo NEAR bar", pa), "Query((foo@1 OR near@2 OR bar@3))");
		CHECK_EQ(run("foo NEAR b", pa), "Query((foo@1 NEAR 11 b@2))");
		CHECK_EQ(run("foo* ADJ bar", w), "Query((foo@1 OR adj@2 OR bar@3))");
		CHECK_EQ(run("foo NEAR bar baz*", w), "Query(((foo@1 NEAR 11 bar@2) OR WILDCARD SYNONYM baz))");
		CHECK_EQ(run("foo AND bar NEAR baz*", w), "Query((foo@1 OR and@2 OR bar@3 OR near@4 OR baz@5))");
		// Fuzzy.
		CHECK_EQ(run("ab~", fz), "Query(EDIT_DISTANCE SYNONYM ab~2)");
		CHECK_EQ(run("Ab~1", fz), "Query(EDIT_DISTANCE SYNONYM ab~1)");
		CHECK_EQ(run("(ab~) cd~ ef", fz), "Query((EDIT_DISTANCE SYNONYM ab~2 OR EDIT_DISTANCE SYNONYM cd~2 OR ef@1))");
		CHECK_EQ(run("ab~1 cd", fz), "Query((ab@1 OR 1@2 OR cd@3))");
		CHECK_EQ(run("ab~x", fz), "Query((ab@1 OR x@2))");
		CHECK_EQ(run("ab~", plain()), "Query(ab@1)");
		// CJK n-grams.
		const config cj = with(flag::cjk_ngram);
		CHECK_EQ(run("中", cj), "Query(中@1)");
		CHECK_EQ(run("中文", cj), "Query((中@1 AND 中文@1 AND 文@1))");
		CHECK_EQ(run("foo中文 bar", cj), "Query((foo@1 OR (中@2 AND 中文@2 AND 文@2) OR bar@3))");
		CHECK_EQ(run("中文", plain()), "Query(中文@1)");
		// Pure NOT.
		const config pn = with(flag::pure_not);
		CHECK_EQ(run("NOT a", pn), "Query((0 * <alldocuments> AND_NOT a@1))");
		CHECK_EQ(run("-a", pn), "Query((0 * <alldocuments> AND_NOT a@1))");
		CHECK_EQ(run("b (NOT a)", pn), "Query((b@1 OR (0 * <alldocuments> AND_NOT a@2)))");
		CHECK_EQ(run("b AND NOT a", pn), "Query((b@1 AND_NOT a@2))");
		CHECK_EQ(run("NOT", pn), "ERROR Syntax: <expression> NOT <expression>");
		// Any-case operators.
		const config ac = with(flag::boolean_any_case);
		CHECK_EQ(run("a and b or c not d xor e", ac), "Query(((a@1 AND b@2) OR ((c@3 AND_NOT d@4) XOR e@5)))");
		CHECK_EQ(run("a and not b", ac), "Query((a@1 AND_NOT b@2))");
		CHECK_EQ(run("a And b", ac), "Query((a@1 AND b@2))");
		CHECK_EQ(run("a near b", ac), "Query((a@1 OR near@2 OR b@3))");
		CHECK_EQ(run("a and b", plain()), "Query((a@1 OR and@2 OR b@3))");
	}
	// The tree can be walked.
	{
		const auto r = parse("a b", plain());
		const auto& root = r.tree.at(r.tree.root());
		CHECK_EQ(std::to_string(int(root.k)), std::to_string(int(kind::or_)));
		CHECK_EQ(std::string(r.tree.text(r.tree.at(root.first))), "a");
	}
	std::printf("queryparser: %s\n", failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
