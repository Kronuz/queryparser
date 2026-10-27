# T2 exceptions

One line per V2 row the parser does not match: `- ROW | KIND | query: reason`, ROW the 1-based line of parse-v2.jsonl.

Kinds:

- `unreachable`: FLAG_WORD_BREAKS needs ICU, which the oracle build lacked (6 rows); the runner excepts them by itself.
- `xapian-2.1`: Xapian 2.1 differs from the 2.0 reference (Q5). None found.
- `stemmer`: V2 used Xapian::Stem and the library uses snowball. None found.
- `open`: a row that fails and is tracked. It counts as failed, is listed on every run, and does not fail the run;
  a new failure, or an open row that now matches, does. The shapes are described in docs/status/queryparser.md.

- ROW | KIND | query: reason`, ROW the 1-based line of parse-v2.jsonl.
Kinds: `xapian-2.1` (2.1 differs from the 2.0 reference, Q5), `stemmer` (V2 used Xapian::Stem, the library uses snowball), `unreachable` (word breaks without ICU; excepted by the runner).

- 448 | open | -"a b" c: tracked
- 467 | open | -a OR b: tracked
- 478 | open | a b -c: tracked
- 479 | open | -a b c: tracked
- 503 | open | a AND -b: tracked
- 577 | open | "a b" NEAR c: tracked
- 578 | open | a NEAR "b c": tracked
- 581 | open | (a) NEAR (b): tracked
- 582 | open | a NEAR +b: tracked
- 584 | open | -a NEAR b: tracked
- 641 | open | a NEAR +b: tracked
- 647 | open | a NEAR +b: tracked
- 1023 | open | a) b: tracked
- 1035 | open | a (b) c): tracked
- 1047 | open | a(b): tracked
- 1050 | open | a-(b): tracked
- 1054 | open | a OR b) c: tracked
- 1069 | open | a NEAR b): tracked
- 1070 | open | (a) NEAR (b: tracked
- 1083 | open | a )b: tracked
- 1084 | open | a( b: tracked
- 1101 | open | (a OR (b: tracked
- 1103 | open | a-(b): tracked
- 1106 | open | (a) NEAR (b: tracked
- 1108 | open | a( b: tracked
- 1114 | open | (a) NEAR (b: tracked
- 1115 | open | a( b: tracked
- 1120 | open | (a) NEAR (b: tracked
- 1121 | open | a( b: tracked
- 1146 | open | a (b) c): tracked
- 1160 | open | a( b: tracked
- 1166 | open | )a*: tracked
- 1170 | open | a (b) c): tracked
- 1173 | open | (a "b c*: tracked
- 1259 | open | a NEAR/3 AND b: tracked
- 1333 | open | a NEAR: tracked
- 1365 | open | NEAR a*: tracked
- 1381 | open | a NEAR/3 NEAR/3 b*: tracked
- 1395 | open | NEAR a*: tracked
- 1411 | open | a NEAR/3 NEAR/3 b*: tracked
- 1425 | open | NEAR a*: tracked
- 1441 | open | a NEAR/3 NEAR/3 b*: tracked
- 1508 | open | "a b" NEAR c: tracked
- 1509 | open | a NEAR "b c": tracked
- 1512 | open | (a) NEAR (b): tracked
- 1513 | open | a NEAR +b: tracked
- 1515 | open | -a NEAR b: tracked
- 1521 | open | a NEAR: tracked
- 1522 | open | NEAR: tracked
- 1575 | open | a-b NEAR c*: tracked
- 1576 | open | a-b NEAR c-d*: tracked
- 1577 | open | "a b" NEAR c*: tracked
- 1578 | open | (a b) NEAR c*: tracked
- 1579 | open | (a) NEAR (b): tracked
- 1580 | open | +a NEAR b*: tracked
- 1583 | open | NEAR a*: tracked
- 1586 | open | NEAR/3 a*: tracked
- 1598 | open | a NEAR b.c.d*: tracked
- 1600 | open | a NEAR/3 (b c) NEAR d*: tracked
- 1610 | open | a-b NEAR c*: tracked
- 1611 | open | a NEAR "b c": tracked
- 1612 | open | a NEAR +b*: tracked
- 1615 | open | NEAR/3 a*: tracked
- 1630 | open | a-b NEAR c*: tracked
- 1631 | open | a NEAR "b c": tracked
- 1632 | open | a NEAR +b*: tracked
- 1635 | open | NEAR/3 a*: tracked
- 1647 | open | a NEAR +b: tracked
- 1648 | open | NEAR: tracked
- 1714 | open | -a OR b: tracked
- 1728 | open | a OR b) c: tracked
- 1731 | open | (a) NEAR (b: tracked
- 1752 | open | a NEAR +b: tracked
- 2181 | open | -the: tracked
- 2189 | open | -a -an: tracked
- 2311 | open | a~-1: tracked
- 2376 | open | a\(b: tracked
- 2551 | open | 中文AND分词: tracked
- 2552 | open | "中文分词": tracked
- 2570 | open | "中文分词": tracked
- 2682 | open | ba~-1: tracked
- 2750 | open | ba~2 bc~1*: tracked
- 2793 | open | ba*~ OR~ bc*~: tracked
- 3056 | open | a~ OR~ b~: tracked
