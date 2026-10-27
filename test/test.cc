// Grammar tests beyond the oracle vectors. Where Xapian's behaviour for a shape is not in
// the vectors, the expected text records what this parser does (V2 will confirm it).
#include "queryparser.hh"

#include <chrono>
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
	CHECK_EQ(P("a (b c)"), "Query((a@1 OR (b@2 OR c@3)))");
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
	CHECK_EQ(P("a NEAR/0 b"), "Query((a@1 OR (near@2 PHRASE 2 0@3) OR b@4))");
	CHECK_EQ(P("a NEAR/4294967295 b"), "Query((a@1 NEAR 2 b@2))");
	CHECK_EQ(P("x a NEAR b"), "Query((x@1 OR a@2 OR near@3 OR b@4))");
	CHECK_EQ(P("a-b NEAR c"), "Query(((a@1 PHRASE 2 b@2) OR (near@3 OR c@4)))");
	// Errors.
	CHECK_EQ(P("a AND"), "ERROR Syntax: <expression> AND <expression>");
	CHECK_EQ(P("AND a"), "ERROR Syntax: <expression> AND <expression>");
	CHECK_EQ(P("a OR OR b"), "ERROR Syntax: <expression> OR <expression>");
	CHECK_EQ(P("-a"), "ERROR Syntax: <expression> NOT <expression>");
	CHECK_EQ(P("a NEAR"), "Query((a@1 OR near@2))");
	CHECK_EQ(P("NEAR a"), "Query((near@1 OR a@2))");
	CHECK_EQ(P("a NEAR b ADJ c"), "Query((a@1 OR near@2 OR b@3 OR adj@4 OR c@5))");
	CHECK_EQ(P("(a AND)"), "ERROR Syntax: <expression> AND <expression>");
	{
		const auto r = parse("ab NOT", plain());
		CHECK_EQ(r.err ? std::to_string(r.err->offset) : "none", "3");
	}
	// Unbalanced brackets.
	CHECK_EQ(P("(a b"), "Query((a@1 OR b@2))");
	CHECK_EQ(P("a) b"), "Query((a@1 OR b@2))");
	// A stray `)` closes nothing, also at the start of the text (a no-op frame pop would be undefined).
	CHECK_EQ(P(")))"), "Query()");
	CHECK_EQ(P(")a"), "Query(a@1)");
	CHECK_EQ(P("a)))"), "Query(a@1)");
	CHECK_EQ(P(") (a b)"), "Query((a@1 OR b@2))");
	// Unclosed brackets close at the end of the text (V2, group parens, stopper none).
	CHECK_EQ(P("(a"), "Query(a@1)");
	CHECK_EQ(P("+(a"), "Query(a@1)");
	CHECK_EQ(P("(a OR (b"), "Query((a@1 OR b@2))");
	CHECK_EQ(P("((a OR b) AND c"), "Query(((a@1 OR b@2) AND c@3))");
	CHECK_EQ(P("("), "Query()");
	CHECK_EQ(P("NOT (a"), "ERROR Syntax: <expression> NOT <expression>");
	CHECK_EQ(P("(a AND"), "ERROR Syntax: <expression> AND <expression>");
	// A mark after an explicit operator (V2, group plusminus): `-` after AND excludes.
	CHECK_EQ(P("a AND -b"), "Query((a@1 AND_NOT b@2))");
	CHECK_EQ(P("a AND -b AND c"), "Query(((a@1 AND_NOT b@2) AND c@3))");
	CHECK_EQ(P("a OR b AND -c"), "Query((a@1 OR (b@2 AND_NOT c@3)))");
	CHECK_EQ(P("a AND -(b c)"), "Query((a@1 AND_NOT (b@2 OR c@3)))");
	CHECK_EQ(P("a AND -\"b c\""), "Query((a@1 AND_NOT (b@2 PHRASE 2 c@3)))");
	CHECK_EQ(P("a AND +b"), "Query((a@1 AND b@2))");
	CHECK_EQ(P("a OR +b"), "Query((a@1 OR b@2))");
	CHECK_EQ(P("+a OR b"), "Query((a@1 OR b@2))");
	CHECK_EQ(P("+a AND b"), "Query((a@1 AND b@2))");
	CHECK_EQ(P("a NOT +b"), "Query((a@1 AND_NOT b@2))");
	CHECK_EQ(P("a NOT -b"), "ERROR Syntax: <expression> NOT <expression>");
	CHECK_EQ(P("a AND NOT -b"), "ERROR Syntax: <expression> AND NOT <expression>");
	// Not in V2: a `-` after OR or XOR is an error rather than a silent inclusion.
	CHECK_EQ(P("a OR -b"), "ERROR Syntax: <expression> OR <expression>");
	CHECK_EQ(P("a XOR -b"), "ERROR Syntax: <expression> XOR <expression>");
	CHECK_EQ(P("a OR -(b)"), "ERROR Syntax: <expression> OR <expression>");
	// A mark consumed by a dropped word does not move to the next operand.
	CHECK_EQ(P("+" + std::string(70, 'a') + " b c"), "Query((b@1 OR c@2))");
	CHECK_EQ(P("-" + std::string(70, 'a') + " b c"), "Query((b@1 OR c@2))");
	CHECK_EQ(P("b +" + std::string(70, 'a') + " c"), "Query((b@1 OR c@2))");
	// Window arithmetic: the distance digits clamp, so a huge NEAR/n cannot wrap.
	CHECK_EQ(P("a NEAR/4294967296 b"), "Query((a@1 NEAR 11 b@2))");
	CHECK_EQ(P("a NEAR/99999999999999999999999999 b"), "Query((a@1 NEAR 3825205248 b@2))");
	CHECK_EQ(P("a ADJ/18446744073709551617 b"), "Query((a@1 PHRASE 2 b@2))");
	{
		config f = plain();
		f.flags = default_flags | flag::fuzzy;
		CHECK_EQ(run("ab~4294967296", f), "Query(EDIT_DISTANCE SYNONYM ab~4294967295)");
		CHECK_EQ(run("ab~99999999999999999999999", f), "Query(EDIT_DISTANCE SYNONYM ab~4294967295)");
	}
	// Flags off: the syntax is plain separators and words.
	{
		config c = plain();
		c.flags = 0;
		CHECK_EQ(run("a AND (b) \"c d\" +e -f-g", c), "Query((a@1 OR and@2 OR b@3 OR (c@4 OR d@5) OR e@6 OR f@7 OR g@8))");
		c.flags = flag::boolean;
		CHECK_EQ(run("a AND b -c", c), "Query((a@1 AND (b@2 OR c@3)))");
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
		// V2 (precedence) differs from the next row: `a OR b c` is `(a OR (b AND c))`; T2 settles it.
		CHECK_EQ(run("a OR b c", c), "Query((a@1 OR (b@2 AND c@3)))");
		c.default_op = kind::xor_;
		CHECK_EQ(run("a b", c), "Query((a@1 XOR b@2))");
		// Marks with default AND: every unmarked item is required (V2, group plusminus).
		c.default_op = kind::and_;
		CHECK_EQ(run("+a b", c), "Query((a@1 AND b@2))");
		CHECK_EQ(run("+a b -c d", c), "Query(((a@1 AND b@2 AND d@4) AND_NOT c@3))");
		CHECK_EQ(run("a AND -b", c), "Query((a@1 AND_NOT b@2))");
		// A default operator that is not one of the four is OR.
		c.default_op = kind::near;
		CHECK_EQ(run("a b c", c), "Query((a@1 NEAR 12 b@2 NEAR 12 c@3))");
		c.default_op = kind::term;
		CHECK_EQ(run("a b", c), "Query((a@1 OR b@2))");
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
		// Under `some` a stemmed term is `Z`, then the field prefix (V2: ZSbarack).
		c.stem = analysis::stem_strategy::some;
		CHECK_EQ(run("title:running", c), "Query(ZSrun@1)");
		CHECK_EQ(run("title:Running", c), "Query(Srunning@1)");
		c.stem = analysis::stem_strategy::all_z;
		CHECK_EQ(run("title:\"runs ran\"", c), "Query((ZSrun@1 PHRASE 2 ZSran@2))");
	}
	// Term text is bounded: a query whose terms exceed max_term_bytes fails with too_long.
	{
		config c = plain();
		c.max_term_bytes = 4;
		auto kind_of = [&](std::string_view q) {
			const auto r = parse(q, c);
			return r.err ? std::to_string(int(r.err->kind)) : std::string("ok");
		};
		const std::string ok = "ok", too_long = std::to_string(int(error_kind::too_long));
		CHECK_EQ(kind_of("abcd"), ok);
		CHECK_EQ(kind_of("abcde"), too_long);
		CHECK_EQ(kind_of("ab cd"), ok);
		CHECK_EQ(kind_of("ab cd e"), too_long);
		c.max_term_bytes = 0;
		CHECK_EQ(kind_of("a"), too_long);
		CHECK_EQ(kind_of(""), ok);
		// Wildcard and edit-distance leaves count too.
		c.max_term_bytes = 4;
		c.flags = default_flags | flag::wildcard | flag::fuzzy;
		CHECK_EQ(kind_of("abcd*"), ok);
		CHECK_EQ(kind_of("abcde*"), too_long);
		CHECK_EQ(kind_of("abcd~"), ok);
		CHECK_EQ(kind_of("abcde~"), too_long);
		c.flags = default_flags;
		// A resolver prefix is copied into every term.
		c.max_term_bytes = 90;
		c.resolver = [](std::string_view name, field_spec& out) {
			if (name != "t") { return false; }
			out.prefix = std::string(30, 'P');
			return true;
		};
		CHECK_EQ(kind_of("t:a-a"), ok);                // 2 x 31 bytes, under 90
		CHECK_EQ(kind_of("t:a-a-a"), too_long);        // 3 x 31 bytes, over 90
		CHECK_EQ(kind_of("t:\"a a a\" b"), too_long);
		c.max_term_bytes = std::size_t(1) << 40;       // clamped to the 32-bit limit
		CHECK_EQ(kind_of("t:a-a"), ok);
		const auto r = parse("t:a-a-a", [&] { config d = plain(); d.max_term_bytes = 90; d.resolver = c.resolver; return d; }());
		CHECK_EQ(r.err ? std::to_string(r.err->offset) : "none", "0");
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
		CHECK_EQ(run("Obam* foo", w), "Query((WILDCARD SYNONYM obam OR foo@2))");
		CHECK_EQ(run("obam* AND x-y*", w), "Query((WILDCARD SYNONYM obam AND (x@2 OR WILDCARD SYNONYM y)))");
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
		CHECK_EQ(run("foo* ADJ bar", w), "Query((foo@1 OR (adj@2 OR bar@3)))");
		CHECK_EQ(run("foo NEAR bar baz*", w), "Query(((foo@1 NEAR 11 bar@2) OR WILDCARD SYNONYM baz))");
		CHECK_EQ(run("foo AND bar NEAR baz*", w), "Query((foo@1 OR and@2 OR bar@3 OR near@4 OR baz@5))");
		// Fuzzy.
		CHECK_EQ(run("ab~", fz), "Query(EDIT_DISTANCE SYNONYM ab~2)");
		CHECK_EQ(run("Ab~1", fz), "Query(EDIT_DISTANCE SYNONYM ab~1)");
		CHECK_EQ(run("(ab~) cd~ ef", fz), "Query((EDIT_DISTANCE SYNONYM ab~2 OR EDIT_DISTANCE SYNONYM cd~2 OR ef@3))");
		CHECK_EQ(run("ab~1 cd", fz), "Query((ab@1 OR (1@2 OR cd@3)))");
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
	// Step 3 (V2): juxtaposed items bind tighter than any explicit operator; only the left operand flattens.
	{
		config c = plain();
		c.default_op = kind::and_;
		CHECK_EQ(run("a OR b c", c), "Query((a@1 OR (b@2 AND c@3)))");
		CHECK_EQ(run("a b OR c", c), "Query(((a@1 AND b@2) OR c@3))");
		CHECK_EQ(run("a b XOR c d", c), "Query(((a@1 AND b@2) XOR (c@3 AND d@4)))");
		CHECK_EQ(run("a NOT b c", c), "Query((a@1 AND_NOT (b@2 AND c@3)))");
		CHECK_EQ(run("a AND (b AND c)", c), "Query((a@1 AND (b@2 AND c@3)))");
		CHECK_EQ(run("a AND b c", c), "Query((a@1 AND (b@2 AND c@3)))");
		CHECK_EQ(run("(a AND b) AND c", c), "Query((a@1 AND b@2 AND c@3))");
		CHECK_EQ(run("a b AND c", c), "Query((a@1 AND b@2 AND c@3))");
		CHECK_EQ(run("+a b c -d", c), "Query(((a@1 AND (b@2 AND c@3)) AND_NOT d@4))");
		// A stray `)` after an operator reads the whole text as plain words, brackets kept.
		CHECK_EQ(run("(a OR b)) AND c", c), "Query((a@1 AND or@2 AND b@3 AND (and@4 AND c@5)))");
		CHECK_EQ(run("a OR )", c), "ERROR Syntax: <expression> OR <expression>");
		// NEAR and ADJ are plain words outside a chain of two plain words.
		CHECK_EQ(run("a NEAR b c", c), "Query((a@1 AND near@2 AND b@3 AND c@4))");
		CHECK_EQ(run("NEAR a", c), "Query((near@1 AND a@2))");
		CHECK_EQ(run("a NEAR/0 b", c), "Query((a@1 AND (near@2 PHRASE 2 0@3) AND b@4))");
		CHECK_EQ(run("a-b NEAR c", c), "Query(((a@1 PHRASE 2 b@2) AND (near@3 AND c@4)))");
		CHECK_EQ(run("a AND NEAR b", c), "ERROR Syntax: <expression> AND <expression>");
		CHECK_EQ(run("a NEAR NEAR", c), "Query((a@1 AND near@2 AND near@3))");
		CHECK_EQ(run("a NEAR/3 b NEAR/5 c", c), "Query((a@1 NEAR 7 b@2 NEAR 7 c@3))");
		CHECK_EQ(run("a NEAR/3 b NEAR c", c), "Query((a@1 NEAR 5 b@2 NEAR 5 c@3))");
		// The error names the tighter of two operators in a row.
		CHECK_EQ(run("a OR AND b", c), "ERROR Syntax: <expression> AND <expression>");
		CHECK_EQ(run("a AND NOT", c), "ERROR Syntax: <expression> AND NOT <expression>");
		// CR2's shapes, still absent from V2: `a OR -b`, `a XOR -b` and `a AND NOT -b` are syntax errors,
		// and `--b` is a plain word.
		CHECK_EQ(run("a OR -b", c), "ERROR Syntax: <expression> OR <expression>");
		CHECK_EQ(run("a XOR -b", c), "ERROR Syntax: <expression> XOR <expression>");
		CHECK_EQ(run("a AND NOT -b", c), "ERROR Syntax: <expression> AND NOT <expression>");
		CHECK_EQ(run("a AND --b", c), "Query((a@1 AND b@2))");
	}
	// Default operators other than AND and OR join the juxtaposed words (V2, group base).
	{
		config c = plain();
		c.flags = flag::phrase | flag::lovehate;
		c.default_op = kind::phrase;
		CHECK_EQ(run("a b", c), "Query((a@1 PHRASE 11 b@2))");
		c.default_op = kind::elite_set;
		CHECK_EQ(run("a b c", c), "Query((a@1 ELITE_SET 10 b@2 ELITE_SET 10 c@3))");
		c.default_op = kind::synonym;
		CHECK_EQ(run("a b", c), "Query((a@1 SYNONYM b@2))");
		c.default_op = kind::max;
		CHECK_EQ(run("a-b c", c), "Query(((a@1 PHRASE 2 b@2) MAX c@3))");
		c.default_op = kind::near;
		CHECK_EQ(run("a-b c", c), "ERROR OP_NEAR and OP_PHRASE only currently support leaf subqueries");
		CHECK_EQ(run("+a b", c), "Query((a@1 AND_MAYBE b@2))");
	}
	// The default prefix, edit distance with its fixed prefix, and the caps that nodes carry.
	{
		config c = plain();
		c.flags |= flag::fuzzy | flag::wildcard | flag::partial;
		c.default_prefix = "title.S";
		c.fuzzy_expansion = {3, expansion_limit::first};
		c.wildcard_expansion = {5, expansion_limit::most_frequent};
		c.partial_expansion = {7, expansion_limit::error};
		CHECK_EQ(run("obama~", c), "Query(EDIT_DISTANCE SYNONYM title.Sobama~2 fixed_prefix_len=7)");
		CHECK_EQ(run("oba*", c), "Query(WILDCARD SYNONYM title.Soba)");
		CHECK_EQ(run("a-b", c), "Query((title.Sa@1 PHRASE 2 title.Sb@2))");
		const auto f = parse("obama~", c);
		const auto& e = f.tree.at(f.tree.root());
		CHECK_EQ(std::to_string(e.max_expansion) + "/" + std::to_string(int(e.limit)) + "/" + std::to_string(e.pos), "3/1/7");
		const auto w = parse("oba*", c);
		CHECK_EQ(std::to_string(w.tree.at(w.tree.root()).max_expansion) + "/" + std::to_string(int(w.tree.at(w.tree.root()).limit)), "5/2");
		const auto p = parse("oba", c);
		CHECK_EQ(std::to_string(p.tree.at(p.tree.at(p.tree.root()).first).max_expansion) + "/" + std::to_string(int(p.tree.at(p.tree.at(p.tree.root()).first).limit)), "7/0");
		c.default_prefix.clear();
		CHECK_EQ(run("obama~", c), "Query(EDIT_DISTANCE SYNONYM obama~2)");
	}
	// Glob wildcards.
	{
		config c = plain();
		c.default_op = kind::and_;
		c.flags |= flag::wildcard | flag::wildcard_single | flag::wildcard_multi;
		CHECK_EQ(run("b?a ba* ba**", c), "Query((WILDCARD SYNONYM b?a AND WILDCARD SYNONYM ba* AND WILDCARD SYNONYM ba**))");
		CHECK_EQ(run("??", c), "Query(WILDCARD SYNONYM ??)");
		CHECK_EQ(run("the*cat", c), "Query(WILDCARD SYNONYM the*cat)");
		CHECK_EQ(run("title:ba**", c), "Query((title@1 AND WILDCARD SYNONYM ba**))");
		CHECK_EQ(run("a* NEAR b*", c), "Query((a@1 AND (near@2 AND b@3)))");
		c.flags = default_flags | flag::wildcard;
		CHECK_EQ(run("b?a ba*", c), "Query((b@1 AND a@2 AND WILDCARD SYNONYM ba))");
	}
	// Stop words: dropped from a run that has other words, kept alone or in a run of only stop words.
	{
		analysis::stopper stop{"the", "a", "an", "and", "of"};
		config c = plain();
		c.stopper = &stop;
		c.default_op = kind::and_;
		CHECK_EQ(run("the cat", c), "Query(cat@2)");
		CHECK_EQ(run("the", c), "Query(the@1)");
		CHECK_EQ(run("the a an", c), "Query((the@1 AND a@2 AND an@3))");
		CHECK_EQ(run("the OR cat", c), "Query((the@1 OR cat@2))");
		CHECK_EQ(run("cat and the dog", c), "Query((cat@1 AND dog@4))");
		CHECK_EQ(run("\"cat of dog\"", c), "Query((cat@1 PHRASE 3 of@2 PHRASE 3 dog@3))");
		CHECK_EQ(run("+the cat", c), "Query((the@1 AND cat@2))");
		CHECK_EQ(run("-running", c), "Query(running@1)");
		c.stop = analysis::stop_strategy::all;
		CHECK_EQ(run("the cat", c), "Query(cat@2)");
		CHECK_EQ(run("the OR cat", c), "Query(cat@2)");
		CHECK_EQ(run("the NOT cat", c), "Query()");
		CHECK_EQ(run("\"cat of dog\"", c), "Query((cat@1 PHRASE 3 dog@3))");
	}
	// An acronym chain that is not an acronym parses in linear time (the quadratic walk took 1.9 s here).
	{
		std::string chain;
		while (chain.size() < 65536) { chain += "A."; }
		chain += "x";
		const auto t0 = std::chrono::steady_clock::now();
		const auto r = parse(chain, plain());
		const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		CHECK_EQ(r.err ? "error" : "ok", "ok");
		CHECK_EQ(secs < 0.5 ? "fast" : "slow " + std::to_string(secs), "fast");
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
