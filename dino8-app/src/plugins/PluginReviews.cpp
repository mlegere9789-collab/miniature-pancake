#include "plugins/PluginReviews.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "util/json_mini.h"

namespace dino8::plugins {

namespace fs = std::filesystem;

namespace {

std::string Escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

double AverageRating(const std::vector<PluginReview>& reviews) {
  if (reviews.empty()) return 0.0;
  long sum = 0;
  for (const PluginReview& r : reviews) sum += r.rating;
  return static_cast<double>(sum) / static_cast<double>(reviews.size());
}

bool ParseReviews(const std::string& json_text, ReviewsByPlugin& out, std::string& error) {
  json::Value root;
  if (!json::Parse(json_text, root, error)) return false;
  if (!root.IsObject()) {
    error = "reviews root is not a JSON object";
    return false;
  }
  const json::Value& sv = root["schema_version"];
  const int schema_version = sv.type == json::Value::Type::Number ? static_cast<int>(sv.number) : 0;
  if (schema_version != 1) {
    error = "unsupported schema_version " + std::to_string(schema_version) + " (this build reads schema_version 1)";
    return false;
  }

  const json::Value& reviews_obj = root["reviews"];
  if (!reviews_obj.IsNull() && !reviews_obj.IsObject()) {
    error = "\"reviews\" is present but not an object";
    return false;
  }

  ReviewsByPlugin parsed;
  for (const auto& kv : reviews_obj.object) {
    const std::string& plugin_id = kv.first;
    const json::Value& arr = kv.second;
    if (!arr.IsArray()) {
      error = "reviews[\"" + plugin_id + "\"] is not an array";
      return false;
    }
    std::vector<PluginReview> list;
    for (size_t i = 0; i < arr.array.size(); ++i) {
      const json::Value& r = arr.array[i];
      if (!r.IsObject()) {
        error = "reviews[\"" + plugin_id + "\"][" + std::to_string(i) + "] is not an object";
        return false;
      }
      const json::Value& rating_v = r["rating"];
      const int rating = rating_v.type == json::Value::Type::Number ? static_cast<int>(rating_v.number) : 0;
      if (rating < 1 || rating > 5) {
        error = "reviews[\"" + plugin_id + "\"][" + std::to_string(i) + "] has an out-of-range rating (" +
                std::to_string(rating) + ", must be 1..5)";
        return false;
      }
      PluginReview review;
      review.reviewer = r["reviewer"].AsString();
      review.rating = rating;
      review.comment = r["comment"].AsString();
      review.date = r["date"].AsString();
      list.push_back(std::move(review));
    }
    parsed[plugin_id] = std::move(list);
  }
  out = std::move(parsed);
  return true;
}

std::string SerializeReviews(const ReviewsByPlugin& reviews) {
  std::ostringstream out;
  out << "{\n  \"schema_version\": 1,\n  \"reviews\": {";
  bool first_plugin = true;
  for (const auto& kv : reviews) {
    if (kv.second.empty()) continue;
    out << (first_plugin ? "\n" : ",\n");
    first_plugin = false;
    out << "    \"" << Escape(kv.first) << "\": [";
    for (size_t i = 0; i < kv.second.size(); ++i) {
      const PluginReview& r = kv.second[i];
      out << (i ? ", " : "") << "{\"reviewer\": \"" << Escape(r.reviewer) << "\", \"rating\": " << r.rating
          << ", \"comment\": \"" << Escape(r.comment) << "\", \"date\": \"" << Escape(r.date) << "\"}";
    }
    out << "]";
  }
  out << (first_plugin ? "" : "\n  ") << "}\n}\n";
  return out.str();
}

bool LoadReviewsFromFile(const std::string& path, ReviewsByPlugin& out, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    out.clear();  // no file yet - nothing has been rated, not an error
    return true;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ParseReviews(ss.str(), out, error);
}

bool SaveReviewsToFile(const std::string& path, const ReviewsByPlugin& reviews, std::string& error) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "could not open " + path + " for writing";
    return false;
  }
  out << SerializeReviews(reviews);
  return true;
}

const std::vector<PluginReview> PluginReviewStore::kNoReviews;

PluginReviewStore& PluginReviewStore::Get() {
  static PluginReviewStore instance;
  return instance;
}

void PluginReviewStore::EnsureLoaded(const std::string& dir) {
  if (loaded_ && dir_ == dir) return;
  dir_ = dir;
  std::string error;
  if (!LoadReviewsFromFile((fs::path(dir_) / "plugin_reviews.json").string(), reviews_, error)) {
    reviews_.clear();  // a corrupt file starts the store fresh rather than blocking the panel
  }
  loaded_ = true;
}

const std::vector<PluginReview>& PluginReviewStore::ReviewsFor(const std::string& plugin_id) const {
  auto it = reviews_.find(plugin_id);
  return it == reviews_.end() ? kNoReviews : it->second;
}

bool PluginReviewStore::AddReview(const std::string& plugin_id, int rating, const std::string& comment,
                                   const std::string& reviewer, const std::string& date, std::string& error) {
  if (plugin_id.empty()) {
    error = "no plugin id given";
    return false;
  }
  if (rating < 1 || rating > 5) {
    error = "rating must be between 1 and 5, got " + std::to_string(rating);
    return false;
  }
  reviews_[plugin_id].push_back({reviewer, rating, comment, date});
  if (dir_.empty()) return true;  // EnsureLoaded was never called - kept in memory only
  std::error_code ec;
  fs::create_directories(dir_, ec);
  return SaveReviewsToFile((fs::path(dir_) / "plugin_reviews.json").string(), reviews_, error);
}

}  // namespace dino8::plugins
