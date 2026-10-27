# T5 exceptions

T5 (`parse-ab.jsonl`) holds the 706 rows on which the first A/B run inside Xapiand found queryparser differing from the Xapian 2.0 that Xapiand ships (`qtext.py`: 1,694 vector queries over 5 fields and 12 operators, 20,304 parses). Each row's expected output is the decision recorded in its `decision` field, and `v20` holds the 2.0 answer where the decision differs from it. One line per row the parser does not match: `- ROW | KIND | query: reason`, ROW the 1-based line of parse-ab.jsonl.

None.
