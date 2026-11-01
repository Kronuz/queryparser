# T5 exceptions

T5 (`parse-ab.jsonl`) holds 706 rows on which queryparser and the Xapian 2.0 that Xapiand ships differ, found by running both inside Xapiand over 1,694 queries, 5 fields and 12 operators (20,304 parses). Each row's expected output is the decision recorded in its `decision` field, and `v20` holds the 2.0 answer where the decision differs from it. One line per row the parser does not match: `- ROW | KIND | query: reason`, ROW the 1-based line of parse-ab.jsonl.

None.
