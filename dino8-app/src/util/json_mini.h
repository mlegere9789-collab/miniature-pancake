// A deliberately tiny JSON reader - just enough to load data/commands.json
// (arrays, objects, strings, numbers, bools, null) without pulling in a
// third-party JSON dependency for a single data file.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dino8::json {

class Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Value> array;
  std::map<std::string, Value> object;

  bool IsNull() const { return type == Type::Null; }
  bool IsString() const { return type == Type::String; }
  bool IsArray() const { return type == Type::Array; }
  bool IsObject() const { return type == Type::Object; }

  // Object member access; returns a shared static Null for missing keys.
  const Value& operator[](const std::string& key) const;
  const Value& operator[](size_t index) const;
  size_t Size() const { return type == Type::Array ? array.size() : object.size(); }
  const std::string& AsString(const std::string& fallback = "") const {
    return IsString() ? string : fallback;
  }
};

// Parses `text`; on failure returns false and fills `error` with a message
// naming the byte offset where parsing stopped.
bool Parse(const std::string& text, Value& out, std::string& error);

}  // namespace dino8::json
