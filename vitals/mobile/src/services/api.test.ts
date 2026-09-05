import { toAbsoluteUrl } from "./api";

describe("toAbsoluteUrl", () => {
  it("returns undefined for an undefined photo url", () => {
    expect(toAbsoluteUrl(undefined)).toBeUndefined();
  });

  it("prefixes a backend-relative path with the API base url", () => {
    expect(toAbsoluteUrl("/uploads/x.jpg")).toBe("http://localhost:4000/uploads/x.jpg");
  });

  it("leaves an already-absolute url untouched", () => {
    expect(toAbsoluteUrl("https://cdn.example.com/photo.jpg")).toBe("https://cdn.example.com/photo.jpg");
  });
});
