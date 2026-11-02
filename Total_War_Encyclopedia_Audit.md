# Audit: *The Total War Series: A Systems Encyclopedia* (78 pp.)

Scope: all 78 pages read. Method: internal consistency, evidence-tag discipline, structure/production, coverage.
Not done: external fact-checking and URL checks (outbound requests from the audit environment were blocked with HTTP 403). Items marked **verify** rest on the auditor's general knowledge and need a second source.

## A. Contradictions between or within parts (high confidence)

| # | Pages | Issue |
|---|---|---|
| A1 | 21 vs 11, 36 | Medieval II is credited to "the Australian studio The Creative Assembly (Brisbane; since disbanded)". Elsewhere CA is the UK studio (p11 "(UK)", p36 Horsham). |
| A2 | 56 vs 68 | Matched-animation example: "dragon lifting a Carnosaur" (p56) vs "a Carnosaur dragging a dragon back" (p68). |
| A3 | 56 vs 37, 68, 76 | Part IV asserts IK for monster foot placement. Parts III and V say no source mentions IK / not verified. |
| A4 | 56 vs 65 | Part IV: LODs switch "by camera distance", with billboards/impostors (unsupported). Part V (Intel/GDC): LOD chosen from average on-screen triangle size, "not simply from distance". |
| A5 | 16 vs 25 | Viking Invasion "three new factions", but at least five are named (Vikings, Scots, Welsh, Mercia, Wessex). |
| A6 | 42 | Attila "56 factions, 40 unplayable, 10 playable" (40 + 10 = 50). |
| A7 | 55 | Warhammer III Chaos factions described as both "via DLC" and "base game lists five Chaos factions". |
| A8 | 64 vs 47 | Part V says Warcore powers Medieval III "and a further unannounced title". Part IV says that title is Warhammer 40,000. |
| A9 | 15 vs 70 | Part II calls the Medieval kill-chance table "inconsistent". The numbers fit a x1.2-per-point curve exactly (1.9x1.2^5 = 4.7, 1.9x1.2^10 = 11.8), as Part V states. |
| A10 | 74 vs 55, 77 | Warhammer III "2.0-era" auto-resolve rework is sourced to GamingBolt (22 Jun 2021) and Legacy of Games (Jul 2021). Warhammer III released Feb 2022 and patch 2.0 was Aug 2022. |
| A11 | 76 vs 47, 67 | Timeline lists a DX12 patch in 2016, but p67 says it was only promised then, and Part IV places it in the Warhammer II era. |
| A12 | 25 | "Playable factions at the start" (Rome) vs the same sentence "not all initially playable". |
| A13 | 48 vs 70 | Part IV: soldiers are "alive/dead". Part V's worked example uses a 69-HP soldier. Rome (p18) uses 1 HP. Needs a per-game statement. |

## B. Evidence-grading problems

1. **Tag definition violated.** Part III defines [Sourced] as "a page I actually fetched" (p28), but applies it to headline-only reads: p34 (Softpedia diary headline), p39 (Darthmod), p41 (Rome II patches 3 and 9). p60 tags the GOSU wishlist headline [Sourced]. p11-12 tags the Shogun "B-grade RTS" story [Sourced], while p26 says "search result description only".
2. **Contradictory provenance.** Softpedia (Rome II mocap, siege AI) and the MCV mocap-studio article are "Fetched and used" on p44, "seen only in search results" on p77, and MCV is "cited only for the caution" on p27. The DBLTAP Empire AI, PC Gamer Warhammer III bug and GDC Vault items (p78) are "seen only", yet p74 states details such as "patch 2.3.0".
3. **Same fact, three grades.** Warhammer III hit chance (35% base, 8-90% clamp): [Sourced lineage] p31, [Background, approximate] p49, Tier A p69.
4. **Comparative table (p43).** Napoleon, Shogun 2 and Rome II are marked "Warscape [S]", but p29 says Wikipedia leaves those blank and p47 grades them [Background]. "EE 90 (Softpedia) [S]" is not supported anywhere in the body.
5. **p64 engine table.** Medieval II "Engine 2" is not supported by p21 or p29. Medieval is flagged as unlabelled and Medieval II is not.
6. Four tag vocabularies (Parts II, III, IV and V), acknowledged on p9, plus untagged sections (Part II 3.6, 3.7, 4.6).

## C. Production and structure

- Process language in finished text: "this session" (pp10-12), "first draft" / "my earlier claim" (pp31, 34, 37, 40), "replaces the first draft after roughly 35 additional searches" (p28), author's "Note on length" (p27).
- Running headers on part-opening pages show the previous part: pp10, 28, 46, 63.
- Links are plain text, with no link annotations. URLs with parentheses break at ")" (pp44, 45, 62). Part III's Wikipedia sources have no URLs. Total War Wiki URLs inconsistently end in `.html`.
- Table cells: "Reworked, 400 maps" in the Siege row (p24). The "(unverified)" naval flag sits on Rome instead of Medieval (p15 vs p24). The Battles row is dated "2012-15" while p59 says support ended 2022. Part I's Part IV summary (p8) omits Arena and Battles.
- In Part II 3.5 (pp15-16), a "Note that..." bullet sits between the Difficulty and Morale bullets.
- TOC: about 60 entries spot-checked, all page numbers correct.

## D. Coverage

- Cover claims "every Creative Assembly title". Most DLC and expansions are missing (Rome II, Empire, Napoleon and Shogun 2 packs). The Warhammer I/II DLC lists are admitted as not retrieved (p55, p61).
- Part V's Rome-era auto-resolve parameters (capture 5-30%, lopsided threshold and modifier, missile separation, naval sink chance) are not cross-referenced from the Part II/III auto-resolve sections, which say the fields are unknown.

## E. Claims to verify externally (not confirmed or refuted here)

- p12, 14: Medieval "ten thousand troop ceiling".
- p20: Rome Remastered "16 new playable factions".
- p32: Empire muskets "effective at only around 200 paces".
- p41: Rome II "three factions each for Rome and Carthage"; Emperor Edition "adding macOS".
- p51: Thrones of Britannia "Best Strategy Game at the 2018 IGDA Awards".
- p53: Three Kingdoms "3.21 million by March 2025"; Troy base-game hero list ("Sarpedon-type designs").
- p55: DLC release dates, especially Blood for the Blood God III (Aug 2022) and Omens of Destruction (Dec 2024).
- p64: "Heavily evolved over 15 years" (2009 to 2025 is 16).

## F. What holds up

- The report flags its own source conflicts honestly (Empire 810,000 sales, mocap cameras 46 vs 56, Rome II units 500 vs 700, Warhammer 40,000 date, Warcore replace vs evolve, Arena shutdown dates).
- Cross-part numbers that agree: elevation (30% at 40 m and 12% at 16 m), armour roll 0.5-1.0, flank/rear 0.6/0.3, 13-second charge decay, 57% hit / 69 HP example, Medieval shield and flank values, Warhammer III release dates, DLC chronology.
