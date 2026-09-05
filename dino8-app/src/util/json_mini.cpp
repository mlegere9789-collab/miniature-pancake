#include "util/json_mini.h"

#include <cstdlib>
#include <cstring>

namespace dino8::json {

namespace {

const Value& NullValue() {
  static const Value null_value;
  return null_value;
}

class Parser {
 public:
  Parser(const std::string& text, std::string& error) : text_(text), error_(error) {}

  bool ParseValue(Value& out) {
    SkipWhitespace();
    if (pos_ >= text_.size()) {
      return Fail("unexpected end of input");
    }
    const char c = text_[pos_];
    if (c == '{') return ParseObject(out);
    if (c == '[') return ParseArray(out);
    if (c == '"') {
      out.type = Value::Type::String;
      return ParseString(out.string);
    }
    if (c == 't' || c == 'f') return ParseBool(out);
    if (c == 'n') return ParseNull(out);
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
    return Fail("unexpected character");
  }

  bool AtEnd() {
    SkipWhitespace();
    return pos_ >= text_.size();
  }

 private:
  bool Fail(const char* message) {
    error_ = std::string(message) + " at byte " + std::to_string(pos_);
    return false;
  }

  void SkipWhitespace() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' ||
            text_[pos_] == '\t')) {
      ++pos_;
    }
  }

  bool ParseObject(Value& out) {
    out.type = Value::Type::Object;
    ++pos_;  // '{'
    SkipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      SkipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("expected object key");
      std::string key;
      if (!ParseString(key)) return false;
      SkipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != ':') return Fail("expected ':'");
      ++pos_;
      Value member;
      if (!ParseValue(member)) return false;
      out.object[key] = std::move(member);
      SkipWhitespace();
      if (pos_ >= text_.size()) return Fail("unterminated object");
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      return Fail("expected ',' or '}'");
    }
  }

  bool ParseArray(Value& out) {
    out.type = Value::Type::Array;
    ++pos_;  // '['
    SkipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      Value element;
      if (!ParseValue(element)) return false;
      out.array.push_back(std::move(element));
      SkipWhitespace();
      if (pos_ >= text_.size()) return Fail("unterminated array");
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      return Fail("expected ',' or ']'");
    }
  }

  static void AppendUtf8(std::string& out, unsigned codepoint) {
    if (codepoint < 0x80) {
      out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
      out += static_cast<char>(0xC0 | (codepoint >> 6));
      out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
      out += static_cast<char>(0xE0 | (codepoint >> 12));
      out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (codepoint >> 18));
      out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
  }

  bool ParseHex4(unsigned& out) {
    if (pos_ + 4 > text_.size()) return Fail("truncated \\u escape");
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      out <<= 4;
      if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
      else return Fail("bad hex digit in \\u escape");
    }
    return true;
  }

  bool ParseString(std::string& out) {
    ++pos_;  // opening quote
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) return Fail("truncated escape");
      const char e = text_[pos_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned codepoint = 0;
          if (!ParseHex4(codepoint)) return false;
          // Surrogate pair.
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF && pos_ + 6 <= text_.size() &&
              text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
            pos_ += 2;
            unsigned low = 0;
            if (!ParseHex4(low)) return false;
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
          }
          AppendUtf8(out, codepoint);
          break;
        }
        default:
          return Fail("bad escape");
      }
    }
    return Fail("unterminated string");
  }

  bool ParseBool(Value& out) {
    out.type = Value::Type::Bool;
    if (text_.compare(pos_, 4, "true") == 0) {
      out.boolean = true;
      pos_ += 4;
      return true;
    }
    if (text_.compare(pos_, 5, "false") == 0) {
      out.boolean = false;
      pos_ += 5;
      return true;
    }
    return Fail("bad literal");
  }

  bool ParseNull(Value& out) {
    out.type = Value::Type::Null;
    if (text_.compare(pos_, 4, "null") == 0) {
      pos_ += 4;
      return true;
    }
    return Fail("bad literal");
  }

  bool ParseNumber(Value& out) {
    out.type = Value::Type::Number;
    const char* start = text_.c_str() + pos_;
    char* end = nullptr;
    out.number = std::strtod(start, &end);
    if (end == start) return Fail("bad number");
    pos_ += static_cast<size_t>(end - start);
    return true;
  }

  const std::string& text_;
  std::string& error_;
  size_t pos_ = 0;
};

}  // namespace

const Value& Value::operator[](const std::string& key) const {
  if (type != Type::Object) return NullValue();
  const auto it = object.find(key);
  return it == object.end() ? NullValue() : it->second;
}

const Value& Value::operator[](size_t index) const {
  if (type != Type::Array || index >= array.size()) return NullValue();
  return array[index];
}

bool Parse(const std::string& text, Value& out, std::string& error) {
  Parser parser(text, error);
  if (!parser.ParseValue(out)) return false;
  if (!parser.AtEnd()) {
    error = "trailing characters after JSON value";
    return false;
  }
  return true;
}

}  // namespace dino8::json
