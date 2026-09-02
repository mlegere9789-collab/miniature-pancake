import { describe, expect, it } from "vitest";
import { lookupSpeciesDormancy, searchSpecies } from "./speciesDormancy";

describe("lookupSpeciesDormancy", () => {
  it("suggests dormancy with northern winter months for a known deciduous species north of the equator", () => {
    const result = lookupSpeciesDormancy("japanese-maple", 40);
    expect(result.known).toBe(true);
    expect(result.habit).toBe("deciduous");
    expect(result.suggestDormant).toBe(true);
    expect(result.months).toEqual([11, 12, 1, 2]);
  });

  it("suggests dormancy with southern winter months for a known deciduous species south of the equator", () => {
    const result = lookupSpeciesDormancy("maple", -33);
    expect(result.suggestDormant).toBe(true);
    expect(result.months).toEqual([5, 6, 7, 8]);
  });

  it("does not suggest dormancy for a known evergreen", () => {
    const result = lookupSpeciesDormancy("boxwood", 40);
    expect(result.known).toBe(true);
    expect(result.habit).toBe("evergreen");
    expect(result.suggestDormant).toBe(false);
    expect(result.months).toEqual([]);
  });

  it("does not suggest dormancy for a known annual", () => {
    const result = lookupSpeciesDormancy("tomato", 40);
    expect(result.known).toBe(true);
    expect(result.habit).toBe("annual");
    expect(result.suggestDormant).toBe(false);
  });

  it("falls back to the hemisphere-only heuristic for an unknown species without suggesting dormancy", () => {
    const result = lookupSpeciesDormancy("some-mystery-plant", 40);
    expect(result.known).toBe(false);
    expect(result.habit).toBeNull();
    expect(result.suggestDormant).toBe(false);
    expect(result.months).toEqual([11, 12, 1, 2]);
  });
});

describe("searchSpecies", () => {
  it("matches by display-name substring, case-insensitively", () => {
    const results = searchSpecies("jap");
    expect(results.map((r) => r.speciesId)).toContain("japanese-maple");
    expect(results.every((r) => r.displayName && r.habit)).toBe(true);
  });

  it("matches by slug substring", () => {
    const results = searchSpecies("maple");
    expect(results.map((r) => r.speciesId)).toEqual(expect.arrayContaining(["japanese-maple", "maple"]));
  });

  it("returns an empty array for a blank query", () => {
    expect(searchSpecies("")).toEqual([]);
    expect(searchSpecies("   ")).toEqual([]);
  });

  it("returns an empty array when nothing matches", () => {
    expect(searchSpecies("xyzzy")).toEqual([]);
  });

  it("respects the limit", () => {
    const results = searchSpecies("e", 3);
    expect(results.length).toBeLessThanOrEqual(3);
  });
});
