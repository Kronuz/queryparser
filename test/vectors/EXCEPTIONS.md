# T1 exceptions

One line per row the parser deliberately does not match: `- query | flags | stem: reason`.

Kind `2.1 versus 2.0`: the oracle is Xapian 2.1 and Xapiand ships 2.0; the parser follows 2.0 (Q5). A capitalized word is stemmed under `some` in 2.0 and left as written in 2.1.

- barack NEAR obama | default|wildcard|partial | some: 2.1 versus 2.0, a capitalized word is stemmed under `some` (2.0), left as written in 2.1
- barack ADJ obama | default|wildcard|partial | some: 2.1 versus 2.0, a capitalized word is stemmed under `some` (2.0), left as written in 2.1
- Barack Obama's | default | some: 2.1 versus 2.0, a capitalized word is stemmed under `some` (2.0), left as written in 2.1
- Barack Obama's | boolean|phrase|lovehate | some: 2.1 versus 2.0, a capitalized word is stemmed under `some` (2.0), left as written in 2.1
- Barack Obama's | default|wildcard|partial | some: 2.1 versus 2.0, a capitalized word is stemmed under `some` (2.0), left as written in 2.1
- Barack Obama's | default|fuzzy | some: 2.1 versus 2.0, a capitalized word is stemmed under `some` (2.0), left as written in 2.1
- Barack Obama's | default|boolean_any_case|pure_not | some: 2.1 versus 2.0, a capitalized word is stemmed under `some` (2.0), left as written in 2.1
- Barack Obama's | default|cjk_ngram | some: 2.1 versus 2.0, a capitalized word is stemmed under `some` (2.0), left as written in 2.1
