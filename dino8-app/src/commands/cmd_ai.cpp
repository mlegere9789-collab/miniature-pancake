// AiSuggestCommand: the interactive surface for src/ml/CommandPredictor.h's
// real, trained neural network - PARITY_MAP.md's "Real AI/ML-based
// modeling assistance" (app_ecosystem) / "AI-assisted modeling or
// scripting" (app_scripting) item. See that header and
// tools/train_command_predictor.cpp for the exact model, training setup
// and corpus; this file only loads the already-trained weights and reports
// a prediction - no training happens here or at runtime.
#include <cstdio>
#include <fstream>

#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "ml/CommandPredictor.h"

namespace dino8::app {

namespace {

// Same multi-candidate search Application::DefaultMarketplaceIndexPath
// (app/Application.cpp) uses for plugin-index/index.json, pointed at the
// trained weights file instead - a build tree, an installed app, and
// running straight from the source tree all find it.
std::string FindWeightsPath(const Application& app) {
  const std::string exe_dir = app.ExeDir();
  const std::vector<std::string> candidates = {
      exe_dir + "/data/ml/command_predictor_weights.txt",
      exe_dir + "/../Resources/data/ml/command_predictor_weights.txt",   // macOS bundle
      exe_dir + "/../share/dino8/data/ml/command_predictor_weights.txt",  // Linux install
      exe_dir + "/../../data/ml/command_predictor_weights.txt",           // build tree
      exe_dir + "/../../../dino8-app/data/ml/command_predictor_weights.txt",
      "data/ml/command_predictor_weights.txt",
  };
  for (const std::string& path : candidates) {
    std::ifstream f(path);
    if (f.good()) return path;
  }
  return std::string();
}

// Loaded at most once per process (training weights never change at
// runtime - LoadWeightsCached just avoids re-parsing the file on every
// AiSuggestCommand call).
bool LoadWeightsCached(const Application& app, dino8::ml::MlpWeights* out, std::string& error) {
  static dino8::ml::MlpWeights cached;
  static bool loaded = false;
  static bool load_failed = false;
  if (loaded) {
    *out = cached;
    return true;
  }
  if (load_failed) {
    error = "AiSuggestCommand: trained weights not found (see tools/train_command_predictor.cpp)";
    return false;
  }
  const std::string path = FindWeightsPath(app);
  if (path.empty()) {
    load_failed = true;
    error = "AiSuggestCommand: could not find data/ml/command_predictor_weights.txt";
    return false;
  }
  if (!dino8::ml::LoadWeights(path, cached, error)) {
    load_failed = true;
    return false;
  }
  loaded = true;
  *out = cached;
  return true;
}

void AiSuggestCommandCmd(CommandContext& ctx) {
  const auto opts = TakeOptionTokens(ctx);
  std::string prev = OptionOr(opts, "command");
  if (prev.empty()) {
    // The real, already-recorded command history (doc/Document.h's
    // ActivityLog, the same durable per-edit record the Activity Log panel
    // shows) - not a separate tracker invented for this feature.
    const auto& log = ctx.Doc().ActivityLog();
    if (!log.empty()) prev = log.back().label;
  }
  if (prev.empty()) {
    ctx.Warn("AiSuggestCommand: no previous command to predict from yet (run a command first, or pass Command=<name>)");
    return;
  }

  dino8::ml::MlpWeights weights;
  std::string error;
  if (!LoadWeightsCached(ctx.App(), &weights, error)) {
    ctx.Warn(error);
    return;
  }

  const auto ranked = dino8::ml::PredictNext(weights, prev, 5);
  if (ranked.empty()) {
    ctx.Print("AiSuggestCommand: '" + prev + "' is outside the trained vocabulary - no suggestion");
    return;
  }
  std::string line = "AiSuggestCommand: after " + prev + ", likely next: ";
  for (size_t i = 0; i < ranked.size(); ++i) {
    if (i) line += ", ";
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%.0f%%", static_cast<double>(ranked[i].second) * 100.0);
    line += ranked[i].first + " (" + pct + ")";
  }
  ctx.Print(line);
}

}  // namespace

void RegisterAiCommands(CommandEngine& e) {
  Reg(e, "AiSuggestCommand", Immediate(AiSuggestCommandCmd), CommandStatus::Implemented,
      "A real, small, trained neural network (src/ml/CommandPredictor.h) predicts the most likely next command given "
      "the last one run (or an explicit Command=<name>). Trained offline from this repo's own test-script corpus - "
      "see tools/train_command_predictor.cpp for the exact setup.");
}

}  // namespace dino8::app
