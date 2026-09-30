// Plug-in ratings/reviews for the Marketplace panel (see MarketplacePanel.cpp
// and plugins/Marketplace.h): a small local store, keyed by
// MarketplaceEntry::id, persisted as JSON in <config>/plugin_reviews.json -
// there is no server for these, a rating only ever reflects what's been
// entered on this machine. Parsing/serialization here is dependency-free
// (no app::Application, like MarketplaceIndex.h's index parsing) so it can
// be unit-tested on its own; PluginReviewStore adds the load-once/share
// singleton on top, the same pattern as plugins::Marketplace.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace dino8::plugins {

struct PluginReview {
  std::string reviewer;  // display name; empty means "Anonymous" (UI's call)
  int rating = 0;        // 1..5
  std::string comment;   // optional
  std::string date;      // caller-supplied, opaque display string (e.g. "2026-09-29 12:00:00")
};

using ReviewsByPlugin = std::map<std::string, std::vector<PluginReview>>;

// Arithmetic mean of `reviews`' ratings; 0.0 for an empty vector (nothing to
// average - callers should check .empty() rather than treat 0.0 as a score).
double AverageRating(const std::vector<PluginReview>& reviews);

// Parses/serializes the on-disk store:
//   {"schema_version": 1, "reviews": {"<plugin id>": [{"reviewer": "...",
//   "rating": 5, "comment": "...", "date": "..."}, ...], ...}}
// ParseIndex returns false (with `error` set) on malformed JSON, an
// unsupported schema_version, or a review missing/out-of-range `rating`.
bool ParseReviews(const std::string& json_text, ReviewsByPlugin& out, std::string& error);
std::string SerializeReviews(const ReviewsByPlugin& reviews);

// An absent file is reported as a normal empty store (`out` cleared, true
// returned) - same "nothing saved yet" treatment as LoadSettings - so only a
// file that exists but fails to parse counts as an error.
bool LoadReviewsFromFile(const std::string& path, ReviewsByPlugin& out, std::string& error);
bool SaveReviewsToFile(const std::string& path, const ReviewsByPlugin& reviews, std::string& error);

// Load-once, share-everywhere store for one reviews file, mirroring
// plugins::Marketplace/plugins::Manager: the panel and the
// PluginMarketplaceRate/PluginMarketplaceReviews commands all read and write
// through this one instance rather than each keeping its own copy.
class PluginReviewStore {
 public:
  static PluginReviewStore& Get();

  // Reads <dir>/plugin_reviews.json into memory. Cheap to call on every
  // panel draw: it only actually re-reads the first time, or after `dir`
  // changes from what was last loaded.
  void EnsureLoaded(const std::string& dir);

  static const std::vector<PluginReview> kNoReviews;
  const std::vector<PluginReview>& ReviewsFor(const std::string& plugin_id) const;
  double AverageRatingFor(const std::string& plugin_id) const { return AverageRating(ReviewsFor(plugin_id)); }

  // Appends one review and saves immediately (so a rating survives even if
  // the app crashes before its next graceful save). Fails without changing
  // anything if `plugin_id` is empty or `rating` is outside 1..5.
  bool AddReview(const std::string& plugin_id, int rating, const std::string& comment, const std::string& reviewer,
                 const std::string& date, std::string& error);

  // Removes the review at `index` (0-based, in the same order ReviewsFor
  // returns - i.e. the order it was added in) from `plugin_id`'s list and
  // saves immediately, the same "survives a crash" guarantee AddReview
  // already gives a newly added review. A plug-in left with no reviews at
  // all is dropped from the on-disk store entirely rather than kept as an
  // empty array, matching what SerializeReviews already does for one. Fails
  // without changing anything if `plugin_id` has no reviews or `index` is
  // out of range.
  bool DeleteReview(const std::string& plugin_id, size_t index, std::string& error);

 private:
  std::string dir_;
  bool loaded_ = false;
  ReviewsByPlugin reviews_;
};

}  // namespace dino8::plugins
