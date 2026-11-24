# Vendor-text scrub of the Dino 8 data files

Branch `w0-scrub1`, answering finding 9 of `parity/oracle/AUDIT/AUDIT.md`: the command catalog
`dino8-app/data/commands.json` shipped help text for about half of the commands and named the vendor
(McNeel, Rhinoceros) hundreds of times. The clean-room rule allows public identifiers only (command
names, option names, aliases). This note lists what was replaced, what was kept and what the owner
still has to decide. No vendor wording is copied into it.

## Rule applied

* Public identifiers (command names, option names, default aliases, JSON keys): kept exactly.
* Vendor-derived prose (descriptions, help pages): replaced or emptied.
* Original text written in this repository: kept.
* Unknown provenance: kept untouched and listed in the last section for the owner.
* Product names (Rhino, Rhinoceros, McNeel) may appear only in short nominative notes: the
  optional `compat` field of a catalog entry (at most 12 words), the OpenNURBS credit in the
  About string, and narrative documents that compare products. Descriptions never carry them.

## Inventory and action

Counts are from the tree at the base commit (`cand5`, catalog of 1,071 entries) and the result.

### dino8-app/data/commands.json (1,071 entries, six keys each)

| Field | Class | Count | Action |
|---|---|---|---|
| `name` | public identifier | 1,071 | kept |
| `options` | public identifier | 1,583 tokens (667 distinct) in 275 entries | kept |
| `description` | vendor-derived | 1,044 non-empty | replaced with an original text of at most 25 words |
| `description` | empty (import left it blank) | 11 | filled with an original text |
| `description` | original (entries added later by this project) | 16 | rewritten to the same 25-word rule (7 were longer) |
| `help` | vendor-derived (verbatim documentation pages, 709,411 bytes) | 501 | emptied; the other 570 were already empty |
| `toolbars` | UI labels | 765 real toolbar lists, 306 "not on toolbars" boilerplate | kept (see unknown list) |
| `menu` | UI labels | 451 non-empty | kept, except 2 paths that named the vendor product; those two were shortened |

Result: 1,071 descriptions, longest 24 words, average 11.5 words; the file shrank from 972,620 to
257,616 bytes; it contains no occurrence of McNeel, Rhinoceros or Rhino.

How the new descriptions were written: from the Dino 8 side only. For each command the registration
in `src/commands/cmd_*.cpp` (factory, prompts, option names, the registration note where one exists)
and, where it was not obvious, the implementing class or function was read. The vendor reference
was not consulted, nothing was paraphrased sentence by sentence, and no vendor example was reused.
A description says what Dino 8's command does today, including its limits (for example a command
that only reports, or one that reuses another command's implementation).

### Other data and text files

| File | Class | Action |
|---|---|---|
| `dino8-app/data/i18n/*.json`, 11 files, 258 keys each | original UI strings (185 keys in `en.json`) | kept |
| the 73 `cmddesc.*` keys in each of the 11 files (803 strings) | derived from the old catalog descriptions | English rewritten (equal to the new catalog text, as `test_i18n_cmddesc` and the `HLP_` section read it back); ten languages re-translated from the new English |
| `about.kernel` in each i18n file | nominative credit for the OpenNURBS library | kept |
| `dino8-app/data/hatchpatterns.pat` and the built-in copy in `src/drafting/HatchLibrary.cpp` | 29 descriptions resembled another vendor's pattern file | rewritten from the pattern geometry (ANSI 31-38, ISO 02-15, seven decorative patterns); `SOLID` and the three legacy Dino 8 patterns were already original and are kept |
| `dino8-app/docs/site/commands.json`, `commands_data.js` | generated copies of the catalog | regenerated with `docs/gen_command_docs.py` (now 1,071 commands) |
| `dino8-app/docs/site/*.html` | original pages | six lines that described the catalog as taken from the vendor's reference, or gave its old size, were reworded |
| `dino8-app/tools/gen_command_catalog.py` | script that built the catalog from the vendor reference | deleted: it is the path by which the vendor text came in |
| `dino8-app/data/changelog.md`, `dino8-app/data/ml/command_predictor_weights.txt` | original (hand-written changelog; weights trained by `tools/train_command_predictor.cpp`, vocabulary = 89 command names) | kept |
| comments and test scripts that quoted catalog sentences | derived | 7 places reworded: `cmd_misc.cpp`, `Panels.cpp`, `SceneObject.h`, `test_adaptive_display.cpp` (comment only), `cmd_srfedit.cpp`, `tests/nurbs_algo_script.txt`, `AUDIT.md` |
| `tests/test_accessibility_tree.cpp` | two fixture strings were old hatch descriptions passed through a builder and read back | fixtures replaced with the new texts; the assertion (row description equals the text passed in) is unchanged |
| placeholder note in `CommandEngine.cpp` | said the full vendor help was in the Help panel | now says the panel shows the description and options |

## Verification

* `tools/check_vendor_text.py` (new): fails when a description is empty or longer than 25 words,
  when McNeel or Rhinoceros appears anywhere in the data files outside the OpenNURBS credit, when
  Rhino appears in a description, help, toolbar or menu text, when the help field is not empty,
  when a vendor URL or page marker appears, or when a description matches one of the removed
  vendor-derived descriptions. The removed texts are held only as digests in
  `docs/clean-room/vendor_text_hashes.txt` (first 20 hex digits of SHA-256 of the normalised
  text: 1,071 digests, 1,042 from the catalog and 29 from the hatch library). `--self-test` plants nine kinds of
  violation in memory and requires all to be reported; run against the catalog as first imported
  (1,055 entries) the checker reports 1,043 digest matches, 500 over-long help fields and 497
  product-name hits.
* Wired into the application smoke run as the section `VENDOR_TEXT`
  (`dino8-app/tests/sections/94_vendor_text.sh`, no application process started).
* Overlap scan: every run of 8 consecutive words (90,111 of them) of the old descriptions and help
  pages was searched for in all text files of the tree, excluding `.git` and `parity/oracle`. After
  the rewording above the only hits are the toolbar and menu label lists (listed below); none in
  source, tests, documentation or tutorials. A 6-word scan finds only everyday phrases.
* Application checks: see the commit message of the last commit and the CI run ids in the job
  report.

## Unknown or owner decisions

1. **Toolbar and menu labels** (`toolbars`, `menu` fields: 765 toolbar lists naming 131 distinct
   toolbars; 451 menu paths, 347 distinct). They are interface labels, not prose, and are shown only
   in the Help panel, but as a whole they reproduce the vendor's toolbar and menu arrangement. Kept.
   If counsel wants them gone, set both fields to empty strings; nothing in the application or its
   tests reads them.
2. **Option tokens** (1,583, in `options`). Public identifiers by nature, but the list was
   extracted from the vendor pages by a crude pattern, so some entries are likely value names rather
   than options. Kept: tests and the Help command print them.
3. **Hatch pattern numbers.** The line-family definitions of all 29 ANSI, ISO and decorative
   patterns are unchanged. ANSI and ISO patterns are named after published standards; the seven
   decorative ones (brick, honeycomb, dots, cross, grass, gravel, net) match the numbers of a
   widely distributed CAD pattern file as far as can be judged without a copy. Provenance is
   unconfirmed. Replacing them with new geometry would change hatch output and test expectations.
4. **Default aliases** in `CommandEngine::InstallDefaultAliases` (49 pairs, described in a code comment as the vendor's
   shipped aliases). Public identifiers; kept.
5. **Narrative documents that name the vendor** (`dino8-app/RHINO8_KILLER_AUDIT.md`,
   `dino8-kernel/README.md`, `dino8-kernel/PARITY_MAP*.md`, the oracle audit files): nominative
   comparisons and the OpenNURBS credit. `PARITY_MAP*.md` was not touched by rule.
6. **Git history.** The old catalog, the help pages and the old i18n and hatch texts remain in the
   repository history (first added in commit `5fae9d9a`). Removing them from history is a decision
   for the owner and counsel; this branch does not rewrite history.
7. **Tutorials, sample plug-ins, templates and other documents** were checked only by the overlap
   scan and the product-name search; authorship of text that matches no catalog sentence cannot be
   proved from the tree.

## Keeping it clean

Write a new command's description from its implementation, in at most 25 words. Do not copy or
paraphrase vendor documentation. Run `python3 tools/check_vendor_text.py` (also part of the
smoke run). A product name belongs only in the optional `compat` field of the catalog entry.
