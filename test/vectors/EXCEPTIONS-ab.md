# T5 exceptions

T5 (`parse-ab.jsonl`) holds the 706 rows on which the first A/B run inside Xapiand found queryparser differing from the Xapian 2.0 that Xapiand ships (T3 A/B, `qtext.py`: 1,694 vector queries over 5 fields and 12 operators, 20,304 parses); the expected output is the shipped 2.0 answer. One line per row the parser does not match: `- ROW | KIND | query: reason`, ROW the 1-based line of parse-ab.jsonl.

Kinds:

- `deliberate`: a choice that differs from 2.0 on purpose.

A lone exclusion (`-a`, `-(a b)`, `-"a b"`, `-a -b`) under BOOLEAN with no stop list: 2.0 reads it as the included word, which turns an exclusion into an inclusion; the parser keeps the NOT syntax error (fail closed, CR2). 25 rows.

- 57 | deliberate | -a: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 58 | deliberate | -(a b): a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 59 | deliberate | -a-b: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 69 | deliberate | -a -b: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 149 | deliberate | -ab: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 213 | deliberate | -(a: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 415 | deliberate | -running: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 455 | deliberate | -the: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 467 | deliberate | -a -an: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 472 | deliberate | -the*: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 473 | deliberate | -a -an*: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 498 | deliberate | -title:a: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 503 | deliberate | -中文: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 511 | deliberate | -the -a: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 513 | deliberate | -ba*: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 517 | deliberate | -banna~: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 611 | deliberate | -"a b": a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 612 | deliberate | -"foo bar": a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 613 | deliberate | -(foo bar): a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 628 | deliberate | -foo: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 629 | deliberate | -a -b -c: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 630 | deliberate | -foo -bar -baz: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 631 | deliberate | -foo -bar: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 632 | deliberate | -a -bar: a lone exclusion is a syntax error here; 2.0 reads it as the included word
- 633 | deliberate | -foo -b: a lone exclusion is a syntax error here; 2.0 reads it as the included word
