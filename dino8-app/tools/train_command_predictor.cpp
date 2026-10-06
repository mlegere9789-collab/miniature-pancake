// Trains src/ml/CommandPredictor.h's real neural network from this repo's
// own test-script corpus (dino8-app/tests/*.txt) - PARITY_MAP.md's "Real
// AI/ML-based modeling assistance" (app_ecosystem) / "AI-assisted modeling
// or scripting" (app_scripting) item's own offline training step. Not part
// of the app or its test suite; run by hand whenever the training corpus
// (the test fixtures) changes meaningfully:
//
//   g++ -std=c++17 -I ../src train_command_predictor.cpp \
//       ../src/ml/CommandPredictor.cpp ../src/util/json_mini.cpp \
//       -o /tmp/train_cmd_predictor
//   /tmp/train_cmd_predictor ..   # argument: the dino8-app directory
//
// Vocabulary: every command name in data/commands.json (the same
// authoritative catalog CommandCatalog.h loads) that is actually invoked
// at least kMinOccurrences times across tests/*.txt - restricting the
// trained vocabulary to commands with real, repeated signal in the corpus,
// rather than all 1055 Rhino 8 command names, most of which never appear
// in this app's own test fixtures at all and would just be dead weight (an
// output unit the network would never get a single real gradient for).
//
// Training examples: for each script file, walk its non-comment, non-
// "@assert"-directive lines in order, take the first whitespace-separated
// token as that line's command name, and keep only tokens that are an
// exact (case-insensitive) match for a vocabulary command name - every
// other token (coordinates, numbers, Yes/No answers, free text, an
// out-of-vocabulary command) is silently skipped, which also breaks the
// (prev, next) chain at that point rather than letting it link across a
// skipped token (see CommandPredictor.h's Train() comment on why).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ml/CommandPredictor.h"
#include "util/json_mini.h"

namespace {

namespace fs = std::filesystem;

std::string ToLowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string ReadWholeFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// The first whitespace-separated token of every line in `path` that isn't
// a "# comment" or an "@expect_..." test-harness directive - exactly the
// set of lines tests/smoke.sh's own script runner treats as real command
// input (see any *_script.txt fixture for the shape).
std::vector<std::string> ExtractLineTokens(const std::string& path) {
  std::vector<std::string> tokens;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string::npos) continue;
    if (line[start] == '#' || line[start] == '@') continue;
    size_t end = line.find_first_of(" \t\r", start);
    if (end == std::string::npos) end = line.size();
    tokens.push_back(line.substr(start, end - start));
  }
  return tokens;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string app_dir = argc > 1 ? argv[1] : ".";
  const std::string commands_json_path = app_dir + "/data/commands.json";
  const std::string tests_dir = app_dir + "/tests";
  const std::string out_path = app_dir + "/data/ml/command_predictor_weights.txt";
  constexpr int kMinOccurrences = 5;
  constexpr int kHiddenSize = 24;
  constexpr int kEpochs = 300;
  constexpr float kLearningRate = 0.15f;

  const std::string json_text = ReadWholeFile(commands_json_path);
  dino8::json::Value root;
  std::string parse_error;
  if (json_text.empty() || !dino8::json::Parse(json_text, root, parse_error) || !root.IsArray()) {
    std::fprintf(stderr, "train_command_predictor: could not parse %s: %s\n", commands_json_path.c_str(), parse_error.c_str());
    return 1;
  }

  std::set<std::string> known_lower;              // every real command name, lowercased
  std::map<std::string, std::string> canonical;    // lowercased -> original-case name
  for (size_t i = 0; i < root.Size(); ++i) {
    const std::string name = root[i]["name"].AsString();
    if (name.empty()) continue;
    const std::string lower = ToLowerAscii(name);
    known_lower.insert(lower);
    canonical[lower] = name;
  }
  if (known_lower.empty()) {
    std::fprintf(stderr, "train_command_predictor: %s named no commands\n", commands_json_path.c_str());
    return 1;
  }

  std::vector<std::string> files;
  for (const auto& entry : fs::directory_iterator(tests_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".txt") files.push_back(entry.path().string());
  }
  std::sort(files.begin(), files.end());  // deterministic file order -> deterministic training
  if (files.empty()) {
    std::fprintf(stderr, "train_command_predictor: no *.txt fixtures found in %s\n", tests_dir.c_str());
    return 1;
  }

  // Pass 1: count real-command occurrences to decide the trained vocabulary.
  std::map<std::string, int> occurrences;
  for (const std::string& path : files)
    for (const std::string& tok : ExtractLineTokens(path)) {
      const std::string lower = ToLowerAscii(tok);
      if (known_lower.count(lower)) ++occurrences[lower];
    }

  std::vector<std::string> vocab;
  for (const auto& [lower, count] : occurrences) if (count >= kMinOccurrences) vocab.push_back(canonical[lower]);
  std::sort(vocab.begin(), vocab.end());
  if (vocab.empty()) {
    std::fprintf(stderr, "train_command_predictor: no command occurs >= %d times across the corpus\n", kMinOccurrences);
    return 1;
  }

  std::map<std::string, int> vocab_index;
  for (size_t i = 0; i < vocab.size(); ++i) vocab_index[ToLowerAscii(vocab[i])] = static_cast<int>(i);

  // Pass 2: one index sequence per file (sequences never cross a file
  // boundary - see Train()'s own comment on why that matters).
  std::vector<std::vector<int>> sequences;
  for (const std::string& path : files) {
    std::vector<int> seq;
    for (const std::string& tok : ExtractLineTokens(path)) {
      const auto it = vocab_index.find(ToLowerAscii(tok));
      seq.push_back(it == vocab_index.end() ? -1 : it->second);
    }
    if (!seq.empty()) sequences.push_back(std::move(seq));
  }

  size_t total_examples = 0;
  for (const std::vector<int>& seq : sequences)
    for (size_t i = 0; i + 1 < seq.size(); ++i)
      if (seq[i] >= 0 && seq[i + 1] >= 0) ++total_examples;

  std::printf("train_command_predictor: vocabulary = %zu commands (>= %d occurrences across %zu script files)\n",
              vocab.size(), kMinOccurrences, files.size());
  std::printf("train_command_predictor: %zu real (prev,next) training examples\n", total_examples);

  const dino8::ml::MlpWeights weights = dino8::ml::Train(vocab, sequences, kHiddenSize, kEpochs, kLearningRate, /*seed=*/1);

  std::error_code ec;
  fs::create_directories(fs::path(out_path).parent_path(), ec);
  std::string save_error;
  if (!dino8::ml::SaveWeights(weights, out_path, save_error)) {
    std::fprintf(stderr, "train_command_predictor: %s\n", save_error.c_str());
    return 1;
  }
  std::printf("train_command_predictor: wrote %s\n", out_path.c_str());
  return 0;
}
