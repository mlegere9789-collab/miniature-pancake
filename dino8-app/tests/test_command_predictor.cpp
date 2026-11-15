// Unit test for PARITY_MAP.md's "Real AI/ML-based modeling assistance"
// (app_ecosystem) / "AI-assisted modeling or scripting" (app_scripting)
// item (src/ml/CommandPredictor.h) - before this there was no neural/
// inference code anywhere in the source.
//
// Three tiers: (1) Forward() is checked against a hand-derived exact
// answer on a tiny, fully-specified network - proving the matrix-multiply/
// ReLU/softmax arithmetic itself is right, not just "doesn't crash"; (2)
// Train() is checked end to end on a tiny synthetic corpus with one
// obviously dominant bigram, proving real gradient descent actually learns
// that structure rather than just running without error; (3) SaveWeights/
// LoadWeights round-trips a trained network through a real file and
// confirms inference is unchanged.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ml/CommandPredictor.h"

using dino8::ml::Forward;
using dino8::ml::LoadWeights;
using dino8::ml::MlpWeights;
using dino8::ml::PredictNext;
using dino8::ml::SaveWeights;
using dino8::ml::Train;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
void CheckNear(double got, double want, double tol, const char* what) {
  Check(std::fabs(got - want) <= tol, what);
}
}  // namespace

int main() {
  // ---- Forward(): exact, hand-derived arithmetic ------------------------
  {
    MlpWeights w;
    w.vocab = {"A", "B"};
    w.hidden_size = 2;
    w.w1 = {{1.0f, 0.0f}, {0.0f, 1.0f}};  // w1[h][v]
    w.b1 = {0.0f, 0.0f};
    w.w2 = {{1.0f, 0.0f}, {0.0f, 1.0f}};  // w2[v][h]
    w.b2 = {0.0f, 0.0f};

    // prev_index=0: hidden = ReLU([1,0]) = [1,0]; logits = [1,0];
    // softmax([1,0]) = [e/(e+1), 1/(e+1)] - a hand-derivable exact answer.
    const std::vector<float> probs = Forward(w, 0);
    Check(probs.size() == 2, "Forward returns one probability per vocab entry");
    const double e = std::exp(1.0);
    CheckNear(probs[0], e / (e + 1.0), 1e-5, "Forward's output-0 probability matches the hand-derived softmax value exactly");
    CheckNear(probs[1], 1.0 / (e + 1.0), 1e-5, "Forward's output-1 probability matches the hand-derived softmax value exactly");
    CheckNear(probs[0] + probs[1], 1.0, 1e-6, "the softmax output sums to 1");

    // A negative bias pushes the hidden unit's pre-activation below zero,
    // so ReLU clamps it to exactly 0 - both outputs then come from b2 alone
    // (both 0 here), so softmax([0,0]) = [0.5, 0.5] exactly.
    w.b1 = {-5.0f, -5.0f};
    const std::vector<float> clamped = Forward(w, 0);
    CheckNear(clamped[0], 0.5, 1e-6, "a hidden unit clamped to exactly 0 by ReLU gives the exact [0.5, 0.5] softmax");
    CheckNear(clamped[1], 0.5, 1e-6, "a hidden unit clamped to exactly 0 by ReLU gives the exact [0.5, 0.5] softmax (second entry)");

    Check(Forward(w, -1).size() == 2 && Forward(w, -1)[0] == 0.0f && Forward(w, -1)[1] == 0.0f,
          "an out-of-range prev_index returns an all-zero vector rather than reading out of bounds");
    Check(w.IndexOf("a") == 0, "IndexOf is case-insensitive");
    Check(w.IndexOf("nonexistent") == -1, "IndexOf reports -1 for a command outside the vocabulary");
  }

  // ---- Train(): real gradient descent learns a dominant bigram ----------
  {
    const std::vector<std::string> vocab = {"Box", "Move", "Delete"};
    // "Box" is followed by "Move" in every one of 40 repeated sequences;
    // "Move"/"Delete" each appear as a prev-command roughly equally often
    // across the corpus, so a network that actually learned the "Box" ->
    // "Move" transition (rather than, say, converging to the output
    // layer's overall marginal frequency) should rank "Move" clearly above
    // "Delete" specifically when conditioned on "Box".
    std::vector<std::vector<int>> sequences;
    for (int i = 0; i < 40; ++i) sequences.push_back({0, 1, 2, 1, 0, 1});  // Box->Move, Move->Delete, Delete->Move, Move->Box, Box->Move
    const MlpWeights trained = Train(vocab, sequences, /*hidden_size=*/4, /*epochs=*/300, /*learning_rate=*/0.2f, /*seed=*/7);

    Check(trained.vocab == vocab, "Train() preserves the vocabulary it was given");
    Check(static_cast<int>(trained.w1.size()) == 4, "Train() produces a w1 matrix with the requested hidden size");

    const auto ranked = PredictNext(trained, "Box", /*top_k=*/3);
    Check(!ranked.empty(), "PredictNext returns a ranked prediction for a known command");
    Check(!ranked.empty() && ranked.front().first == "Move",
          "training on a corpus where Box is always followed by Move actually learns that the top prediction after Box is Move");
    Check(!ranked.empty() && ranked.front().second > 0.6f,
          "the learned Box->Move probability is not just barely ahead, but a genuinely confident prediction");

    const auto ranked_unknown = PredictNext(trained, "NotACommand", 3);
    Check(ranked_unknown.empty(), "PredictNext returns nothing for a command outside the trained vocabulary");
  }

  // ---- SaveWeights / LoadWeights: a real file round trip -----------------
  {
    const std::vector<std::string> vocab = {"Line", "Circle", "Arc"};
    std::vector<std::vector<int>> sequences;
    for (int i = 0; i < 20; ++i) sequences.push_back({0, 1, 0, 1, 2});
    const MlpWeights trained = Train(vocab, sequences, 3, 150, 0.2f, 3);

    const char* path = "test_command_predictor_weights_tmp.txt";
    std::string error;
    Check(SaveWeights(trained, path, error), "SaveWeights writes a real file");

    MlpWeights loaded;
    Check(LoadWeights(path, loaded, error), "LoadWeights reads the file SaveWeights just wrote");
    Check(loaded.vocab == trained.vocab, "the loaded vocabulary matches exactly");
    Check(loaded.hidden_size == trained.hidden_size, "the loaded hidden size matches exactly");

    const std::vector<float> before = Forward(trained, 0);
    const std::vector<float> after = Forward(loaded, 0);
    Check(before.size() == after.size(), "pre- and post-round-trip predictions have the same shape");
    bool all_close = before.size() == after.size();
    for (size_t i = 0; all_close && i < before.size(); ++i) all_close = std::fabs(before[i] - after[i]) < 1e-4;
    Check(all_close, "inference through the saved-then-loaded weights matches the original trained weights closely");

    std::remove(path);

    MlpWeights bogus;
    Check(!LoadWeights("this_file_does_not_exist_hopefully.txt", bogus, error), "LoadWeights fails cleanly on a missing file");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
