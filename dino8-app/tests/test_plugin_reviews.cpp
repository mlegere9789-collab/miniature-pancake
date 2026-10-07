// Unit test for the plug-in ratings/reviews local store
// (src/plugins/PluginReviews.cpp): JSON parsing/serialization, schema
// validation, and PluginReviewStore's add/persist/reload round-trip.
//
// Deliberately standalone, like tests/test_plugin_marketplace.cpp - this
// only ever touches a plain file path, never app::Application, so it can
// run without the kernel/UI the Marketplace panel's "Rate this plug-in" UI
// itself needs; that UI is visually simple enough (a slider, two text
// fields, a button that calls the same AddReview tested here) not to need
// its own smoke.sh end-to-end script on top of this.
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "plugins/PluginReviews.h"

using dino8::plugins::AverageRating;
using dino8::plugins::LoadReviewsFromFile;
using dino8::plugins::ParseReviews;
using dino8::plugins::PluginReview;
using dino8::plugins::PluginReviewStore;
using dino8::plugins::ReviewsByPlugin;
using dino8::plugins::SaveReviewsToFile;
using dino8::plugins::SerializeReviews;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
}  // namespace

int main() {
  // ---- AverageRating -------------------------------------------------
  Check(AverageRating({}) == 0.0, "AverageRating of no reviews is 0.0");
  {
    std::vector<PluginReview> rs = {{"a", 5, "", ""}, {"b", 3, "", ""}, {"c", 4, "", ""}};
    Check(AverageRating(rs) == 4.0, "AverageRating averages the ratings (5+3+4)/3 == 4.0");
  }

  // ---- ParseReviews: malformed / invalid inputs -----------------------
  ReviewsByPlugin parsed;
  std::string error;
  Check(!ParseReviews("{not json", parsed, error), "ParseReviews rejects malformed JSON");
  Check(!ParseReviews("[]", parsed, error), "ParseReviews rejects a non-object root");
  Check(!ParseReviews("{\"reviews\": {}}", parsed, error), "ParseReviews rejects a missing schema_version");
  Check(!ParseReviews("{\"schema_version\": 2, \"reviews\": {}}", parsed, error),
        "ParseReviews rejects an unsupported schema_version");
  Check(!ParseReviews("{\"schema_version\": 1, \"reviews\": []}", parsed, error),
        "ParseReviews rejects a \"reviews\" value that isn't an object");
  Check(!ParseReviews("{\"schema_version\": 1, \"reviews\": {\"hellodino\": {}}}", parsed, error),
        "ParseReviews rejects a per-plugin value that isn't an array");
  Check(!ParseReviews("{\"schema_version\": 1, \"reviews\": {\"hellodino\": [{\"rating\": 0}]}}", parsed, error),
        "ParseReviews rejects a rating of 0 (out of the 1..5 range)");
  Check(!ParseReviews("{\"schema_version\": 1, \"reviews\": {\"hellodino\": [{\"rating\": 6}]}}", parsed, error),
        "ParseReviews rejects a rating of 6 (out of the 1..5 range)");
  Check(!ParseReviews("{\"schema_version\": 1, \"reviews\": {\"hellodino\": [{}]}}", parsed, error),
        "ParseReviews rejects a review with no rating at all (defaults to 0, out of range)");

  // ---- ParseReviews: a real minimal valid store, and the round trip ----
  const std::string minimal =
      "{\"schema_version\": 1, \"reviews\": {\"hellodino\": ["
      "{\"reviewer\": \"Alice\", \"rating\": 5, \"comment\": \"Great!\", \"date\": \"2026-09-29\"},"
      "{\"reviewer\": \"\", \"rating\": 3, \"comment\": \"\", \"date\": \"2026-09-28\"}"
      "]}}";
  Check(ParseReviews(minimal, parsed, error), "ParseReviews accepts a valid minimal store (" + error + ")");
  Check(parsed.size() == 1 && parsed.count("hellodino") == 1, "ParseReviews reads the one plugin id present");
  if (parsed.count("hellodino")) {
    const auto& rs = parsed["hellodino"];
    Check(rs.size() == 2, "ParseReviews reads both reviews for hellodino");
    if (rs.size() == 2) {
      Check(rs[0].reviewer == "Alice" && rs[0].rating == 5 && rs[0].comment == "Great!" && rs[0].date == "2026-09-29",
            "the first review's fields parsed correctly");
      Check(rs[1].reviewer.empty() && rs[1].rating == 3, "an anonymous (empty reviewer) review still parses");
    }
  }

  const std::string serialized = SerializeReviews(parsed);
  ReviewsByPlugin round_tripped;
  Check(ParseReviews(serialized, round_tripped, error), "SerializeReviews's own output re-parses (" + error + ")");
  Check(round_tripped.size() == parsed.size() && round_tripped["hellodino"].size() == parsed["hellodino"].size(),
        "the parse -> serialize -> parse round trip preserves the review count");
  if (round_tripped["hellodino"].size() == 2) {
    Check(round_tripped["hellodino"][0].reviewer == "Alice" && round_tripped["hellodino"][0].rating == 5,
          "the round trip preserves a review's reviewer/rating");
  }

  // A quote or backslash in a comment must not corrupt the JSON it's
  // embedded in on the way back out.
  {
    ReviewsByPlugin tricky;
    tricky["x"].push_back({"Bob \"the builder\"", 4, "Has a \\ and a \" in it", "2026-09-29"});
    const std::string s = SerializeReviews(tricky);
    ReviewsByPlugin back;
    Check(ParseReviews(s, back, error), "SerializeReviews escapes quotes/backslashes so the result still parses (" + error + ")");
    if (back.count("x") && back["x"].size() == 1) {
      Check(back["x"][0].reviewer == "Bob \"the builder\"" && back["x"][0].comment == "Has a \\ and a \" in it",
            "quotes and backslashes survive the round trip unescaped correctly");
    }
  }

  // ---- LoadReviewsFromFile / SaveReviewsToFile ------------------------
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "dino8_test_plugin_reviews";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  const std::string path = (dir / "plugin_reviews.json").string();

  ReviewsByPlugin missing;
  Check(LoadReviewsFromFile(path, missing, error) && missing.empty(),
        "LoadReviewsFromFile on a nonexistent file returns true with an empty store (nothing rated yet)");

  ReviewsByPlugin to_save;
  to_save["hellodino"].push_back({"Carol", 2, "meh", "2026-09-01"});
  Check(SaveReviewsToFile(path, to_save, error), "SaveReviewsToFile writes the file (" + error + ")");
  ReviewsByPlugin loaded;
  Check(LoadReviewsFromFile(path, loaded, error) && loaded["hellodino"].size() == 1 &&
            loaded["hellodino"][0].reviewer == "Carol" && loaded["hellodino"][0].rating == 2,
        "LoadReviewsFromFile reads back exactly what SaveReviewsToFile wrote");

  // ---- PluginReviewStore: add/persist/reload, and the singleton's own
  // "only re-read when `dir` changes" cache ----------------------------
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  PluginReviewStore& store = PluginReviewStore::Get();
  store.EnsureLoaded(dir.string());
  Check(store.ReviewsFor("hellodino").empty(), "a freshly EnsureLoaded'd store has no reviews for an unrated plugin");
  Check(store.AverageRatingFor("hellodino") == 0.0, "AverageRatingFor an unrated plugin is 0.0");

  Check(!store.AddReview("", 5, "", "", "", error), "AddReview rejects an empty plugin id");
  Check(!store.AddReview("hellodino", 0, "", "", "", error), "AddReview rejects rating 0");
  Check(!store.AddReview("hellodino", 6, "", "", "", error), "AddReview rejects rating 6");

  Check(store.AddReview("hellodino", 5, "Solid plug-in", "Dana", "2026-09-29", error),
        "AddReview accepts a valid rating (" + error + ")");
  Check(store.ReviewsFor("hellodino").size() == 1, "the store now has one review for hellodino");
  Check(store.AddReview("hellodino", 3, "", "", "2026-09-29", error), "AddReview accepts a second, anonymous review");
  Check(store.ReviewsFor("hellodino").size() == 2, "the store now has two reviews for hellodino");
  Check(store.AverageRatingFor("hellodino") == 4.0, "AverageRatingFor reflects both added ratings ((5+3)/2 == 4.0)");

  // AddReview must persist to disk immediately, not just keep state in
  // memory - a fresh store instance loading the same directory should see
  // both reviews without any explicit save call.
  ReviewsByPlugin reread;
  Check(LoadReviewsFromFile((dir / "plugin_reviews.json").string(), reread, error) &&
            reread["hellodino"].size() == 2,
        "AddReview persisted both reviews to disk without a separate save step");

  // EnsureLoaded on the same directory again must not clobber the
  // in-memory additions with a stale on-disk copy (it's a no-op once
  // already loaded for that directory).
  store.EnsureLoaded(dir.string());
  Check(store.ReviewsFor("hellodino").size() == 2, "EnsureLoaded on an already-loaded directory is a no-op, not a reload");

  // ---- PluginReviewStore::DeleteReview --------------------------------
  Check(!store.DeleteReview("nosuchplugin", 0, error), "DeleteReview rejects a plugin id with no reviews at all");
  Check(!store.DeleteReview("hellodino", 2, error), "DeleteReview rejects an out-of-range index (only 0 and 1 exist)");
  Check(store.ReviewsFor("hellodino").size() == 2, "a rejected DeleteReview call changed nothing");

  Check(store.DeleteReview("hellodino", 0, error), "DeleteReview removes Dana's review at index 0 (" + error + ")");
  Check(store.ReviewsFor("hellodino").size() == 1, "one review remains after deleting the other");
  Check(store.ReviewsFor("hellodino")[0].reviewer.empty() && store.ReviewsFor("hellodino")[0].rating == 3,
        "the surviving review is the anonymous one that was at index 1, now shifted to index 0");

  // DeleteReview must persist immediately too, the same as AddReview.
  ReviewsByPlugin reread_after_delete;
  Check(LoadReviewsFromFile((dir / "plugin_reviews.json").string(), reread_after_delete, error) &&
            reread_after_delete["hellodino"].size() == 1,
        "DeleteReview persisted the removal to disk without a separate save step");

  Check(store.DeleteReview("hellodino", 0, error), "DeleteReview removes the last remaining review");
  Check(store.ReviewsFor("hellodino").empty(), "hellodino has no reviews left after deleting the last one");
  Check(!store.DeleteReview("hellodino", 0, error),
        "DeleteReview on a plugin just emptied out behaves like one that was never rated - rejected, not a crash");

  // Emptying a plugin's review list must drop it from the on-disk store
  // entirely (SerializeReviews already skips empty entries), not leave a
  // dangling "hellodino": [] behind.
  ReviewsByPlugin reread_after_empty;
  Check(LoadReviewsFromFile((dir / "plugin_reviews.json").string(), reread_after_empty, error) &&
            reread_after_empty.count("hellodino") == 0,
        "deleting a plugin's last review drops it from the persisted store rather than leaving an empty array");

  fs::remove_all(dir, ec);

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
