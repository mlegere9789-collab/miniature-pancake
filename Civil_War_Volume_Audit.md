# Audit: *The American Civil War: Research Volume and Game Design Blueprint* (524 pp.)

**Scope.** Every page, 1-524, was read in full by eight parallel reviewers working from per-page text of the PDF (Drive file `Civil_War_Research_and_Game_Design_Volume-compressed.pdf`, 1.38 MB). Part XI's claims about the Total War PDF were checked against that PDF page by page.

**Method and limits.**
- The reviewers audited internal consistency, evidence-tag discipline, structure and production, and checked facts against their own knowledge of the history. No outside source or URL could be fetched (the sandbox blocks outbound requests). Factual findings are therefore graded CONFIRMED / LIKELY / VERIFY, and none has been checked against a primary source.
- I re-checked these against the page text myself: the Reno/Mansfield contradiction (p.57), Fort Fisher "480 yards (about 2,500 feet)" (p.111), the Part V splice (p.179), the stray "End of document" (p.505), Quantrill "March 1862" (pp.226, 323), Pea Ridge "March 8-9" (p.270) and Mill Springs "January 20" (p.268). All were as reported. I did not re-check the rest.
- **Page numbers.** All page numbers below are PDF page indices, which equal the printed "Page N" footers. Part title pages: I 23, II 27, III 68, IV 119, V 172, VI 213, VII 257, VIII 298, IX 330, X 376, XI 412, XII 445, XIII 472, XIV 506, XV 517.
- **Table of contents.** A reviewer parsed 795 TOC entries and found every one on its stated page. The only anomalies are the Part V gap (below) and a Part X TOC that omits its Section III, Section IV and Sources.

---

## 1. Highest-severity issues

1. **Part V is spliced and missing a large block (p.179, TOC p.8-9).** Section 4.4 (Red River) runs straight into Resaca text, and the next heading is 6.4. Sections 4.5 to 6.3 and all of section 5 are absent. They would have covered Pleasant Hill, the Bailey dams, Camden, Brice's Crossroads, Tupelo, Fort Pillow, Johnsonville, Rocky Face, Snake Creek Gap and Resaca. Parts of the design lists (pp.207-208), the order-of-battle table (p.209) and the "checked" list (p.211) still cite that missing research.
2. **Part XIV (game concept) and XV (clean-room plan) contradict the research parts and each other.**
   - *Schedule* (XIV p.515 vs XII p.466): XIV has a vertical slice at months 2-6 and content scale-up ending at month 36. XII's own planning scenario has pre-production at 9-12 months, a slice at 12-18, alpha at about 30 and ship at 36-48 months, with 40-80 people. XIV states no team size, and XV p.523 copies the XIV months.
   - *Simulation model* (XIV pp.510, 513, 516 vs XII p.459): XIV insists "every shot is simulated". XII recommends a volley-level model for CPU reasons, and XIV cites nothing to support its departure from that.
   - *Quick resolve* (XIV p.513): it is "the same deterministic engine", yet a coarser tier is used when hardware cannot manage the full simulation. That breaks identical results for multiplayer, replays and shared forecasts.
   - *Zone R* (XV p.520 vs p.23): XV puts the agents that produced this volume in the restricted Zone R. Part I says the same orchestrating assistant then wrote the concept and the plan, and XIV embeds Total War audit conclusions throughout. The wall XV defines is already breached by the document.
   - *Constants* (XV p.519, p.523): XV makes this volume "Phase 0" input, but Part XI p.440 reproduces Warhammer III and Empire constants (35% base hit, 8-90% clamp, range 70, accuracy 40).
   - *Patent and counsel timing* (XIII pp.481, 501 vs XV pp.522-523): XIII says retain counsel and run patent/freedom-to-operate review before building. XV puts patent searches in phase 4, months 18-36.
   - *MOSS* (XIII p.496 vs p.490): p.496 lists MOSS in CI gates, while p.490 says it is non-commercial only and uploads code to Stanford.
   - *NEC v. Intel* (XIII p.484 vs p.476): p.484 repeats the claim that NEC "won because it documented a clean room", which p.476 retracts.
3. **Part XI (the Total War audit) misreports the PDF it audits in about 15 places.** About 150 of roughly 170 checked citations were accurate. The failures include:
   - Wrong section for the Rome II stances (p.414).
   - "Hold squares" where the PDF says bayonet regiments should be kept on the wings (p.416).
   - A Shogun AI cite filed under Medieval (p.421).
   - Shield-block roll listed under multiplayer (p.426).
   - Skirmisher stats cited where the PDF says they are unresolved (p.438).
   - "Rams" and "breech-loading", which the PDF never says (pp.436-437).
   - Medieval naval auto-resolve stated flat where the PDF marks it unverified.
   - The PDF's [Sourced] tag mapped to "Tier A, treat as solid".
   - "Part V is the only part with real engineering sources", which is overstated.

   Part XI also missed process text and the "every title" overclaim in that PDF.
4. **Process text is left in the finished volume, in every part.** Examples: "first draft", "my earlier figure", "this session", "I could not verify", "the coordinator asked", "The items you flagged" (p.254), "[CORRECTED: my first draft implied…]" (XIII pp.475-477). Part III p.108 says the Sailor's Creek bullets were "partly scrambled", and they still are: the Lockett Farm and Marshall's Crossroads labels are swapped. A stray "End of document" sits at p.505 with 19 pages to go.
5. **Evidence tags are applied unreliably.**
   - Part II defines [V]/[U] but uses none; its one explicit "verified" claim (p.29, Baltimore riot) is wrong.
   - Parts III, IV, V, VI, VII and IX state flat, untagged, claims their own logs list as unverified.
   - [V]/[VERIFIED] sits on content marked "from memory".
   - In Part XIII, encyclopedia-only sourcing is tagged "Verified" (Lotus, Sega, Tetris), though p.503 says the opinion texts were not retrieved.

---

## 2. Errors by part (most severe first; page numbers are PDF)

**Front matter, Part I, Part II (pp.1-67)**
- p.57: Reno is called "the second Union corps commander to die" and Mansfield died three days later. Reno (14 Sep) was first and Mansfield second.
- p.48: "five of its corps south of Richmond" with V Corps north of the river; the army had five corps, so four were south.
- p.63: Lee's "war is so terrible" quote appears twice, one inverted. The standard form is "It is well that war is so terrible, or we should grow too fond of it."
- p.59 vs p.57: "roughly 25 percent" Confederate loss at Antietam does not fit 30,646 engaged (34%).
- p.29: Baltimore riot "five soldiers killed", tagged verified. The standard figure is four.
- p.42/66 vs p.44: Johnston's strength given as 72,000 (called a correction) vs 60,000 weeks later.
- Likely errors: Cedar Mountain divisions given as brigades (p.52), a "McClellan order to wait" for Sumner (p.58), Stephen D. Lee at Antietam with Jackson (p.59), "Johnston's Quaker guns at Yorktown" (p.31), Hill vs Early at Williamsburg (p.43), Roanoke Island "four forts" with three named (p.38).
- p.26 says the war was fought over slavery, but the secession narrative (pp.28-29) never names slavery as a cause.
- p.23 vs p.24: Part I says each agent re-opened web sources, but Part XI was read from the PDF and Parts XIV-XV came from the orchestrator.
- Stated word counts are accurate within 1-4% except Part VI (stated 22,200, about 23,080) and Part XV (stated 3,900, about 3,340).

**Part III, Eastern Theater 1863-65 (pp.68-118)**
- p.70 and p.24: the ledger tally "37 rows (35 V, 2 P)" is wrong; counting gives 32 V, 2 P, 2 U.
- p.111: Fort Fisher land face "480 yards (about 2,500 feet)"; 480 yards is 1,440 feet. The gun counts also conflict (47 on the land face vs about 47 total).
- p.111: first Fort Fisher force is "XXIV and X Corps"; X Corps was abolished 3 Dec 1864.
- p.73: Greenback Raid placed at Brandy Station; it was near Kearneysville, W.Va.
- p.97: Hayes as a Crook division commander at Third Winchester.
- p.108: scrambled Sailor's Creek labels (above); p.100: "Baldy Smith's miss" describes the wrong action.
- Internal conflicts: Pickett step-off "~2 pm" vs a 1 pm cannonade (p.83), Hazel Grove May 2 vs May 3 (pp.75-76), May 5 council count (p.76), Rienzi renamed "Winchester" (p.105 vs p.99), Five Forks and Waynesboro numbers (pp.99, 105-106), Fort Gregg defenders (p.117 vs p.106).
- Overclaims: "only president ever under fire" (p.96), Sedgwick "highest-ranking officer killed" (p.91), "bloodiest day" for May 3 (p.76).
- Sensitive-history gap: Plymouth (p.88) omits the executions of captured Black soldiers.

**Part IV, Western Theater 1861-63 (pp.119-171)**
- p.151 vs p.149: Port Gibson divisions listed as brigades; p.162 vs p.164: contradictory accounts of who ordered Longstreet to Knoxville.
- p.152: Grant "settled for a siege"; he assaulted twice to avoid one. p.154: "external lines" for what was an interior-line maneuver.
- p.134-135: Kennebec, not Pinola, turned back at New Orleans; 13, not 14, ships passed.
- p.130: Johnston's "100,000" (the usual quote is "a million"). p.133: the gunboat stop was on the evening of Day 1.
- Dates and totals that disagree internally: Island No. 10 (three different spans), Vicksburg siege, Knoxville (Nov 17-Dec 4 vs Nov 4-Dec 5), Lexington TN (Dec 17 vs 18), Champion Hill and Iuka casualty sums that do not add.
- p.153: Milliken's Bend omits the treatment of captured Black soldiers.

**Part V, Western Theater 1863-65 (pp.172-212)**
- Section splice (above).
- p.215 (Part VI) Wilson's Creek: Union 285+873+186 = 1,344, not 1,317.
- p.180: "Stephen Hood" should be John Bell Hood. p.198: Ames's division was white, not USCT. p.205: Booth "captured"; he was killed.
- p.205 vs p.220: Buckner's role in the Trans-Mississippi surrender contradicted between parts.
- p.187-188: Wheeler "captured Dalton" in August; the garrison was taken in October. p.189: Mobile Bay channel on the wrong side of the torpedoes.
- Fort Fisher: Terry's force 8,000 (p.198) vs 9,632 (pp.111, 248).

**Part VI, Beyond the Main Armies (pp.213-256)**
- p.226 and p.323 (Part VIII): Quantrill commissioned after "First Independence (March 1862)"; the battle was August 1862, after the April 1862 Partisan Ranger Act.
- p.247: Lee in command at Savannah in April 1862 (he had gone to Richmond). p.234: Cushing's boat crew figures do not add.
- p.250: Logan classed as low-competence political general, against Part V.
- p.229: Bear River massacre euphemised, while Sand Creek is called a massacre.

**Parts VII-VIII, Commanders (pp.257-329)**
- p.270: Hancock lost the 1880 popular vote by about 40,000; it was under 10,000 votes (0.1%).
- Dates wrong against the volume's own other pages: Pea Ridge "March 8-9" (p.270; also given as 6-8 on p.215 and 7-8 on p.399; correct is 7-8), Mill Springs "Jan 20" (p.268, vs Jan 19 on pp.125, 324, 408), Davis captured "May 9" (p.302, vs May 10 on pp.110, 243, 393, 407).
- p.262: Cameron "First Secretary of War" (first under Lincoln).
- p.304: George Rains is not Gabriel's older brother.
- West Point class ranks wrong for Longstreet, Van Dorn, Burnside, Reynolds, Ewell.
- p.312: Forrest brigadier "end of 1862" (July 1862). p.267: Ledlie's division "took 3,800 casualties" (that is the whole Union loss).
- Gettysburg rosters conflate brigades (Stonewall Brigade, "Smith's Georgia brigade", Florida regiments under Posey vs Perry, Ramseur/Iverson, Part VIII pp.310-327).
- Name spelling: Hincks vs Hinks (p.290 vs pp.100, 106, 113). Grierson's raid: 800 miles in 17 days (p.278) vs 600 elsewhere.
- Part VIII entries show deleted-slot residue ("126. (Removed.)", "123. This slot previously held names I could not support").

**Parts IX-X, Politics and Gazetteer (pp.330-411)**
- p.369 timeline: secession of four states "April 1861", Homestead Act "July 1862", Hunter order "March-April 1862". All three are wrong against the volume's own earlier pages.
- p.335 Stanton insulted Lincoln in "1857" (1855). p.338 sentence contradicts itself on the June vs September 1862 governors' conferences.
- p.374 citations garbled: *The Cause of All Nations* is by Doyle alone; "Dray / Durden" is garbled.
- p.372 "corrects" the national debt to $2B (the body's $2.7B was right). p.401 Indiana quorum reversed. p.388 Columbus KY and Island No. 10 order reversed. p.398 third gunboat at Sabine Pass not in Part VI.
- Gazetteer dates differ from Parts III-IV (Petersburg, Chancellorsville, Chickamauga, Island No. 10, Knoxville, Fort Fisher) and from its own other pages. Overland losses "~65,000" (p.345) vs "~55,000" (Part III p.94).
- p.376 cites a verification log in "Part IV" that is its own Section IV (p.408).

**Part XII, Technology dossier (pp.445-471)**
- GPU budget lines sum to 14.0-18.5 ms against a 16.6 ms frame (p.453).
- "Three tiers" (pp.464, 466) vs four (A-D, pp.450, 463). Prototype validated in months 3-4 (p.464) vs engine gate at months 1-2 (p.466).
- Acoustic shadows called "confirmed" on one author's presentation (pp.446, 457, 471).
- Epic "production-ready" motion matching claim is sourced to ComputerBase and later hedged (pp.446 vs 450, 454, 462).
- Doubtful bibliographic and technical details to verify: JASA vs *Applied Acoustics*, GDC vs SIGGRAPH 2014 fog talk, ".69 Harpers Ferry rifle", Napoleon rate of fire.
- Smoothbore effective range 100 yd (Part XI p.436) vs 50 yd (p.459).

**Part XIII (pp.472-505)**
- Chapterhouse "June 2013 jury trial" is likely a summary-judgment ruling (p.501).
- Several 2026 legal specifics (EU "Regulation 2026/1744", Bartz final approval) could not be confirmed (pp.488, 497-498).
- Case-ledger sourcing overstated (see above); duplicate paragraph (p.487); broken Zone table (p.488); TOC/heading mismatch for Section 5 (p.497).

**Parts XIV-XV (pp.506-524)**
- "Balloons later in the war" (p.508) vs Part II (balloon corps disbanded in 1863).
- "Nothing in this part is a historical claim to be verified" (p.506), though it makes many checkable claims.
- XIV "adopt/adapt/avoid" classifications disagree with Part XI's (XIV p.507 vs XI pp.415, 418, 439).
- Phases: XIV has five and XV six (p.523 vs p.515). "The brief" is never defined (XIV pp.513-516, XV p.524).
- Zone and gate schemes differ between XIII (Zones A-E, 7 gates) and XV (Zones R/S/I/A/L, G0-G7), and "Zone A" means different things.
- "A period the Total War series has not covered" (XV p.524) ignores Empire, Napoleon and Shogun 2 / Fall of the Samurai, which Part XI itself discusses.

---

## 3. Production and structure (whole volume)
- The running header on every part-opening page shows the previous part (pp.27, 68, 119, 172, 213, 257, 298, 330, 376, 412, 445, 472, 506, 517).
- Internal heading labels are inconsistent ("Part N" in II, IV, VI, VII; "Section" in III, VIII, IX, X; bare numerals in V), so the volume's Part numbers collide with internal ones. p.24 says headings appear as "Section" to avoid this.
- Stray markdown (`**…**`, p.43), stranded headings at page foot (pp.33, 45, 63, 64), broken table headers ("Statu / s", p.68; "Mid-186 / 3", p.114; Part XII table p.450), all-"1" numbered lists (p.446-447).
- Repeated or truncated text in several places (Part IV pp.162-164 and others).
- Tag vocabularies differ: [V]/[U] (I, II, VII), [Verified]/[UNVERIFIED] (V), "(checked)" (VI), plus inline variants.

## 4. What checked out
- All 795 TOC entries match their pages.
- Many casualty tables sum correctly (Chancellorsville, Spotsylvania, the Crater, Stones River, Chickasaw Bayou, Vicksburg, Wilderness, Shiloh, Chickamauga, Fort Fisher II).
- The large majority of dates, orders of battle and commanders across Parts II-X, and the legal ledger in Part XIII (Atari v. Nintendo, Sega v. Accolade, Connectix, Lotus, Google v. Oracle, Baker, Jack Daniel's and others), are correct as far as the reviewers' knowledge goes.
- About 150 of Part XI's roughly 170 citations to the Total War PDF are accurate, and Part XII's arithmetic (sizes, voxels, shot rates) is correct.

## 5. Recommended fixes, by priority
1. Restore the missing Part V sections (4.5-6.3 and 5) from source, or remove everything that cites them.
2. Strip all process language and the stray "End of document" before release; reconcile the Part XIV/XV schedule and simulation claims with Part XII.
3. Replace one tag system volume-wide and re-grade claims (no [V] on "from memory" text; no flat statements for items in the logs as unverified).
4. Correct the dated errors in the tables above, and reconcile cross-part dates (Fort Fisher, Island No. 10, Knoxville, Pea Ridge, Mill Springs, Davis, Quantrill).
5. Fix running headers, internal heading labels and broken tables.
6. Re-verify Part XI's citations to the Total War PDF.
7. Have a human or primary-source check on every item graded LIKELY/VERIFY above before publishing corrections.
