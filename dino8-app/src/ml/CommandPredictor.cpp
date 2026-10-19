#include "ml/CommandPredictor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>

namespace dino8::ml {

namespace {

std::string ToLowerAscii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::vector<float> Softmax(const std::vector<float>& logits) {
  std::vector<float> out(logits.size());
  if (logits.empty()) return out;
  const float max_logit = *std::max_element(logits.begin(), logits.end());
  float sum = 0.0f;
  for (size_t i = 0; i < logits.size(); ++i) {
    out[i] = std::exp(logits[i] - max_logit);  // subtract max for numeric stability
    sum += out[i];
  }
  if (sum > 0.0f) for (float& v : out) v /= sum;
  return out;
}

}  // namespace

int MlpWeights::IndexOf(const std::string& command) const {
  const std::string needle = ToLowerAscii(command);
  for (size_t i = 0; i < vocab.size(); ++i) {
    if (ToLowerAscii(vocab[i]) == needle) return static_cast<int>(i);
  }
  return -1;
}

std::vector<float> Forward(const MlpWeights& weights, int prev_index) {
  const size_t vocab_size = weights.vocab.size();
  std::vector<float> probs(vocab_size, 0.0f);
  if (prev_index < 0 || static_cast<size_t>(prev_index) >= vocab_size) return probs;
  if (weights.hidden_size <= 0) return probs;

  // hidden = ReLU(W1[:, prev_index] + b1) - the one-hot input selects
  // exactly one column of W1, so the full vocab-sized matrix-vector
  // product collapses to picking that column, same arithmetic result as
  // multiplying by a true one-hot vector but without materializing one.
  std::vector<float> hidden(static_cast<size_t>(weights.hidden_size));
  for (int h = 0; h < weights.hidden_size; ++h) {
    const float pre = weights.w1[static_cast<size_t>(h)][static_cast<size_t>(prev_index)] +
                       weights.b1[static_cast<size_t>(h)];
    hidden[static_cast<size_t>(h)] = std::max(0.0f, pre);
  }

  std::vector<float> logits(vocab_size, 0.0f);
  for (size_t v = 0; v < vocab_size; ++v) {
    float sum = weights.b2[v];
    for (int h = 0; h < weights.hidden_size; ++h) sum += weights.w2[v][static_cast<size_t>(h)] * hidden[static_cast<size_t>(h)];
    logits[v] = sum;
  }
  return Softmax(logits);
}

std::vector<std::pair<std::string, float>> PredictNext(const MlpWeights& weights, const std::string& prev_command,
                                                         size_t top_k) {
  const int idx = weights.IndexOf(prev_command);
  if (idx < 0) return {};
  const std::vector<float> probs = Forward(weights, idx);
  std::vector<std::pair<std::string, float>> ranked;
  ranked.reserve(probs.size());
  for (size_t v = 0; v < probs.size(); ++v) ranked.emplace_back(weights.vocab[v], probs[v]);
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  if (ranked.size() > top_k) ranked.resize(top_k);
  return ranked;
}

MlpWeights Train(const std::vector<std::string>& vocab, const std::vector<std::vector<int>>& sequences,
                  int hidden_size, int epochs, float learning_rate, unsigned seed) {
  MlpWeights w;
  w.vocab = vocab;
  w.hidden_size = hidden_size;
  const size_t vocab_size = vocab.size();

  std::mt19937 rng(seed);
  // Small random init (uniform, scaled by fan-in) - the usual "break
  // symmetry, start near linear" neural-net init, nothing exotic.
  std::uniform_real_distribution<float> init(-0.3f, 0.3f);
  w.w1.assign(static_cast<size_t>(hidden_size), std::vector<float>(vocab_size));
  w.b1.assign(static_cast<size_t>(hidden_size), 0.0f);
  w.w2.assign(vocab_size, std::vector<float>(static_cast<size_t>(hidden_size)));
  w.b2.assign(vocab_size, 0.0f);
  for (auto& row : w.w1) for (float& x : row) x = init(rng);
  for (auto& row : w.w2) for (float& x : row) x = init(rng);

  // Build every (prev_index, next_index) training pair up front - within
  // one sequence only, never crossing a sequence boundary (see this
  // function's own header comment).
  std::vector<std::pair<int, int>> examples;
  for (const std::vector<int>& seq : sequences) {
    for (size_t i = 0; i + 1 < seq.size(); ++i) {
      if (seq[i] < 0 || seq[i + 1] < 0) continue;  // a filtered/unknown token breaks the chain at that point
      examples.emplace_back(seq[i], seq[i + 1]);
    }
  }

  std::vector<size_t> order(examples.size());
  std::iota(order.begin(), order.end(), 0);

  std::vector<float> hidden(static_cast<size_t>(hidden_size));
  std::vector<float> hidden_grad(static_cast<size_t>(hidden_size));
  std::vector<float> logits(vocab_size);

  for (int epoch = 0; epoch < epochs; ++epoch) {
    std::shuffle(order.begin(), order.end(), rng);
    for (size_t oi : order) {
      const int prev = examples[oi].first;
      const int next = examples[oi].second;

      // ---- forward -----------------------------------------------------
      for (int h = 0; h < hidden_size; ++h) {
        const float pre = w.w1[static_cast<size_t>(h)][static_cast<size_t>(prev)] + w.b1[static_cast<size_t>(h)];
        hidden[static_cast<size_t>(h)] = std::max(0.0f, pre);
      }
      for (size_t v = 0; v < vocab_size; ++v) {
        float sum = w.b2[v];
        for (int h = 0; h < hidden_size; ++h) sum += w.w2[v][static_cast<size_t>(h)] * hidden[static_cast<size_t>(h)];
        logits[v] = sum;
      }
      const std::vector<float> probs = Softmax(logits);

      // ---- backward (softmax + cross-entropy: dL/dlogit = prob - onehot) -
      // ---- then plain fully-connected backprop down to the one active
      // ---- column of w1 (the input is one-hot, so every other column's
      // ---- gradient is exactly zero and is skipped rather than computed
      // ---- and discarded). ------------------------------------------------
      std::fill(hidden_grad.begin(), hidden_grad.end(), 0.0f);
      for (size_t v = 0; v < vocab_size; ++v) {
        const float dlogit = probs[v] - (v == static_cast<size_t>(next) ? 1.0f : 0.0f);
        w.b2[v] -= learning_rate * dlogit;
        for (int h = 0; h < hidden_size; ++h) {
          hidden_grad[static_cast<size_t>(h)] += dlogit * w.w2[v][static_cast<size_t>(h)];
          w.w2[v][static_cast<size_t>(h)] -= learning_rate * dlogit * hidden[static_cast<size_t>(h)];
        }
      }
      for (int h = 0; h < hidden_size; ++h) {
        // ReLU'(pre) is 1 where hidden > 0, else 0 (and the gradient is
        // exactly zero through a clamped unit either way).
        const float d_pre = hidden[static_cast<size_t>(h)] > 0.0f ? hidden_grad[static_cast<size_t>(h)] : 0.0f;
        w.b1[static_cast<size_t>(h)] -= learning_rate * d_pre;
        w.w1[static_cast<size_t>(h)][static_cast<size_t>(prev)] -= learning_rate * d_pre;
      }
    }
  }

  return w;
}

bool SaveWeights(const MlpWeights& weights, const std::string& path, std::string& error) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    error = "command predictor: could not open '" + path + "' for writing";
    return false;
  }
  out << "DINO8_CMDPREDICTOR_V1\n";
  out << weights.vocab.size() << ' ' << weights.hidden_size << '\n';
  for (const std::string& name : weights.vocab) out << name << '\n';
  out.precision(9);
  for (const std::vector<float>& row : weights.w1) {
    for (size_t i = 0; i < row.size(); ++i) out << (i ? " " : "") << row[i];
    out << '\n';
  }
  for (size_t i = 0; i < weights.b1.size(); ++i) out << (i ? " " : "") << weights.b1[i];
  out << '\n';
  for (const std::vector<float>& row : weights.w2) {
    for (size_t i = 0; i < row.size(); ++i) out << (i ? " " : "") << row[i];
    out << '\n';
  }
  for (size_t i = 0; i < weights.b2.size(); ++i) out << (i ? " " : "") << weights.b2[i];
  out << '\n';
  return static_cast<bool>(out);
}

bool LoadWeights(const std::string& path, MlpWeights& out, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "command predictor: could not open '" + path + "'";
    return false;
  }
  std::string header;
  if (!std::getline(in, header) || header != "DINO8_CMDPREDICTOR_V1") {
    error = "command predictor: '" + path + "' is not a DINO8_CMDPREDICTOR_V1 weights file";
    return false;
  }
  size_t vocab_size = 0;
  int hidden_size = 0;
  if (!(in >> vocab_size >> hidden_size)) {
    error = "command predictor: '" + path + "' has a malformed size header";
    return false;
  }
  std::string rest_of_line;
  std::getline(in, rest_of_line);

  MlpWeights w;
  w.hidden_size = hidden_size;
  w.vocab.resize(vocab_size);
  for (size_t i = 0; i < vocab_size; ++i) {
    if (!std::getline(in, w.vocab[i])) {
      error = "command predictor: '" + path + "' is truncated in its vocab list";
      return false;
    }
  }
  w.w1.assign(static_cast<size_t>(hidden_size), std::vector<float>(vocab_size));
  for (auto& row : w.w1) for (float& x : row) if (!(in >> x)) { error = "command predictor: '" + path + "' is truncated in w1"; return false; }
  w.b1.assign(static_cast<size_t>(hidden_size), 0.0f);
  for (float& x : w.b1) if (!(in >> x)) { error = "command predictor: '" + path + "' is truncated in b1"; return false; }
  w.w2.assign(vocab_size, std::vector<float>(static_cast<size_t>(hidden_size)));
  for (auto& row : w.w2) for (float& x : row) if (!(in >> x)) { error = "command predictor: '" + path + "' is truncated in w2"; return false; }
  w.b2.assign(vocab_size, 0.0f);
  for (float& x : w.b2) if (!(in >> x)) { error = "command predictor: '" + path + "' is truncated in b2"; return false; }

  out = std::move(w);
  return true;
}

}  // namespace dino8::ml
