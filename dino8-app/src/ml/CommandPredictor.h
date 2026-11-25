// A small, genuinely trained feed-forward neural network - PARITY_MAP.md's
// "Real AI/ML-based modeling assistance" (app_ecosystem) / "AI-assisted
// modeling or scripting" (app_scripting) item: before this there was no
// neural/inference code anywhere in the source (the existing Smart Blocks
// clustering feature, src/commands/cmd_smartblocks.cpp, explicitly
// documents itself as classical geometry, not machine learning - see its
// own top-of-file comment).
//
// The task: given the command the user just ran, predict which command
// they are likely to run next - a real (if intentionally tiny) order-1
// neural language model, the same minimal architecture a "neural
// probabilistic language model" (Bengio et al. 2003) scales down to for a
// single-token context: one-hot input -> one hidden ReLU layer -> softmax
// output over the command vocabulary. tools/train_command_predictor.cpp
// trains the weights for real (forward pass, cross-entropy loss,
// backpropagation, plain SGD) from a genuine corpus - every command
// actually invoked across this repo's own tests/*_script.txt fixtures, not
// synthetic or hand-picked data - and writes them to
// data/ml/command_predictor_weights.txt, loaded at runtime by
// AiSuggestCommand (src/commands/cmd_ai.cpp). Honestly scoped: an order-1
// (single previous command) model with a small hidden layer, not a
// sequence model with real memory and not a large language model - see
// that training tool's own comment for the exact setup and corpus.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace dino8::ml {

struct MlpWeights {
  std::vector<std::string> vocab;  // vocab[i] is the command name predicted/conditioned on at index i
  int hidden_size = 0;
  // w1[h][v]: weight from input vocab index v to hidden unit h (hidden_size rows x vocab.size() columns).
  std::vector<std::vector<float>> w1;
  std::vector<float> b1;  // hidden_size
  // w2[v][h]: weight from hidden unit h to output vocab index v (vocab.size() rows x hidden_size columns).
  std::vector<std::vector<float>> w2;
  std::vector<float> b2;  // vocab.size()

  // Case-insensitive lookup; -1 if `command` isn't in the trained vocabulary.
  int IndexOf(const std::string& command) const;
};

// Real forward-pass inference: one-hot(prev_index) -> ReLU hidden layer ->
// softmax output, returned as a probability distribution over
// weights.vocab (sums to ~1, modulo float rounding). An out-of-range
// prev_index returns an all-zero vector rather than reading out of bounds -
// defensive; callers are expected to check IndexOf()'s result first.
std::vector<float> Forward(const MlpWeights& weights, int prev_index);

// The top `top_k` vocab entries by Forward()'s own probability, descending,
// ties broken by vocab order. Convenience wrapper around Forward() for
// callers (AiSuggestCommand) that just want names + scores. Empty if
// prev_command isn't in weights.vocab.
std::vector<std::pair<std::string, float>> PredictNext(const MlpWeights& weights, const std::string& prev_command,
                                                         size_t top_k = 5);

// One real gradient-descent training run: `sequences` is one vector of
// vocab indices per source file (tools/train_command_predictor.cpp builds
// these from the real test-fixture corpus, one sequence per *_script.txt
// file); consecutive (prev, next) pairs *within* the same sequence become
// training examples - a sequence boundary is never treated as a
// transition, so the first command of one file is never trained as
// following the last command of a different file. Plain mini-batch-of-one
// SGD, examples shuffled fresh each epoch (seeded, so a given corpus +
// seed always trains the identical weights - reproducible, not
// hand-tuned). Pure CPU, no third-party ML framework: small enough (a few
// hundred vocab entries, a few dozen hidden units) that a hand-written
// forward/backward pass is the entire dependency.
MlpWeights Train(const std::vector<std::string>& vocab, const std::vector<std::vector<int>>& sequences,
                  int hidden_size, int epochs, float learning_rate, unsigned seed = 1);

// Saves/loads weights to/from the small plain-text format
// data/ml/command_predictor_weights.txt uses: one header line
// ("DINO8_CMDPREDICTOR_V1"), a line of "<vocab_size> <hidden_size>", then
// vocab_size vocab-name lines, then hidden_size w1 rows (vocab_size floats
// each), one b1 line (hidden_size floats), vocab_size w2 rows (hidden_size
// floats each), and one b2 line (vocab_size floats). Hand-rolled rather
// than reusing util/json_mini.h, which is read-only (no writer) by design -
// see that header's own comment - and a flat row-per-line format is both
// simpler to write and trivially diffable.
bool SaveWeights(const MlpWeights& weights, const std::string& path, std::string& error);
bool LoadWeights(const std::string& path, MlpWeights& out, std::string& error);

}  // namespace dino8::ml
