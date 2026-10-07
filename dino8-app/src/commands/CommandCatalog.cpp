#include "commands/CommandCatalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>

#include "util/json_mini.h"

namespace dino8::app {

namespace {

// Score a fuzzy/subsequence match of `query_lower` against a command name,
// given both its original casing (`name_orig`, for word-boundary detection)
// and its lowercased form (`name_lower`, for case-insensitive comparison;
// `query_lower` is already lowercase too). Returns false if `query_lower`'s
// characters do not all appear in `name_lower` in order. On success, writes
// a relevance score to `score_out`: higher is better, and the scale is
// deliberately layered so exact / prefix / contiguous-substring matches
// always outrank a scattered subsequence match, no matter how well that
// subsequence scores internally (this is what keeps existing prefix-match
// behaviour unchanged).
bool ScoreFuzzyMatch(const std::string& name_orig, const std::string& name_lower,
                     const std::string& query_lower, int& score_out) {
  if (query_lower.empty()) return false;
  const size_t n = name_lower.size();
  const size_t m = query_lower.size();
  if (m > n) return false;

  // Fast paths: exact match, prefix match, contiguous substring. These are
  // the cases the previous prefix-only autocomplete already handled, so
  // they keep ranking at (or above) the top.
  if (name_lower == query_lower) {
    score_out = 1000000;
    return true;
  }
  if (name_lower.compare(0, m, query_lower) == 0) {
    score_out = 900000 - static_cast<int>(n);
    return true;
  }
  const size_t sub = name_lower.find(query_lower);
  if (sub != std::string::npos) {
    score_out = 800000 - static_cast<int>(sub) * 10 - static_cast<int>(n);
    return true;
  }

  // Scattered subsequence: classic two-row DP (the technique VS Code's and
  // fzf's command-palette scorers use). best[j] = best score matching the
  // first j query characters using name characters seen so far, allowing
  // the match to end anywhere; run[j] = best score where the match is
  // required to end exactly at the name character just considered (so the
  // next character, if it also matches, can claim the consecutive-run
  // bonus). One row of each is all that's needed since row i only depends
  // on row i-1 (walked in decreasing j order) - O(n*m) per candidate,
  // trivially fast even across 1000+ short command names on every
  // keystroke.
  constexpr int kNegInf = std::numeric_limits<int>::min() / 2;
  std::vector<int> best(m + 1, kNegInf);
  std::vector<int> run(m + 1, kNegInf);
  best[0] = 0;
  for (size_t i = 1; i <= n; ++i) {
    // A word-boundary start: the first character, right after a non-letter
    // (a separator dino8 command names don't really use, but harmless to
    // check), or a camelCase capital following a lowercase letter.
    const bool camel = i > 1 && std::islower(static_cast<unsigned char>(name_orig[i - 2])) &&
                       std::isupper(static_cast<unsigned char>(name_orig[i - 1]));
    const bool sep_before = i > 1 && std::isalnum(static_cast<unsigned char>(name_orig[i - 2])) == 0;
    const bool boundary = (i == 1) || camel || sep_before;
    // Walk j downward so best[j-1]/run[j-1] read this iteration's previous
    // row before they get overwritten for the current one.
    for (size_t j = std::min(m, i); j >= 1; --j) {
      int new_run = kNegInf;
      if (name_lower[i - 1] == query_lower[j - 1]) {
        const int char_score = 1 + (boundary ? 8 : 0);
        const int from_best = best[j - 1] > kNegInf ? best[j - 1] + char_score : kNegInf;
        const int from_run = run[j - 1] > kNegInf ? run[j - 1] + char_score + 5 : kNegInf;
        new_run = std::max(from_best, from_run);
      }
      run[j] = new_run;
      if (new_run > best[j]) best[j] = new_run;
      if (j == 1) break;
    }
  }
  if (best[m] <= kNegInf / 2) return false;  // not a subsequence
  // Below the substring tier, and biased toward shorter names for equally
  // good matches.
  score_out = 500000 + best[static_cast<size_t>(m)] * 100 - static_cast<int>(n);
  return true;
}

}  // namespace

std::string ToLower(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool CommandCatalog::Load(const std::vector<std::string>& candidate_paths, std::string& error) {
  for (const std::string& path : candidate_paths) {
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    std::stringstream buffer;
    buffer << in.rdbuf();
    json::Value root;
    if (!json::Parse(buffer.str(), root, error)) {
      error = path + ": " + error;
      return false;
    }
    commands_.clear();
    index_.clear();
    for (size_t i = 0; i < root.Size(); ++i) {
      const json::Value& e = root[i];
      CommandInfo info;
      info.name = e["name"].AsString();
      if (info.name.empty()) continue;
      info.description = e["description"].AsString();
      info.toolbars = e["toolbars"].AsString();
      info.menu = e["menu"].AsString();
      info.help = e["help"].AsString();
      const json::Value& opts = e["options"];
      for (size_t k = 0; k < opts.Size(); ++k) info.options.push_back(opts[k].AsString());
      index_[ToLower(info.name)] = commands_.size();
      commands_.push_back(std::move(info));
    }
    loaded_from_ = path;
    return !commands_.empty();
  }
  error = "commands.json not found in any candidate location";
  return false;
}

const CommandInfo* CommandCatalog::Find(const std::string& name) const {
  const auto it = index_.find(ToLower(name));
  return it == index_.end() ? nullptr : &commands_[it->second];
}

std::vector<const CommandInfo*> CommandCatalog::WithPrefix(const std::string& prefix, size_t limit) const {
  std::vector<const CommandInfo*> out;
  const std::string p = ToLower(prefix);
  for (const CommandInfo& c : commands_) {
    if (ToLower(c.name).compare(0, p.size(), p) == 0) {
      out.push_back(&c);
      if (out.size() >= limit) break;
    }
  }
  return out;
}

std::vector<const CommandInfo*> CommandCatalog::FuzzyMatch(const std::string& query, size_t limit) const {
  std::vector<const CommandInfo*> out;
  if (query.empty()) return out;
  const std::string q = ToLower(query);
  struct Scored {
    const CommandInfo* info;
    int score;
  };
  std::vector<Scored> scored;
  scored.reserve(commands_.size());
  for (const CommandInfo& c : commands_) {
    int score = 0;
    if (ScoreFuzzyMatch(c.name, ToLower(c.name), q, score)) scored.push_back({&c, score});
  }
  std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.info->name.size() != b.info->name.size()) return a.info->name.size() < b.info->name.size();
    return a.info->name < b.info->name;
  });
  out.reserve(std::min(limit, scored.size()));
  for (size_t i = 0; i < scored.size() && i < limit; ++i) out.push_back(scored[i].info);
  return out;
}

std::vector<const CommandInfo*> CommandCatalog::Search(const std::string& text) const {
  std::vector<const CommandInfo*> out;
  const std::string t = ToLower(text);
  for (const CommandInfo& c : commands_) {
    if (t.empty() || ToLower(c.name).find(t) != std::string::npos ||
        ToLower(c.description).find(t) != std::string::npos) {
      out.push_back(&c);
    }
  }
  return out;
}

}  // namespace dino8::app
