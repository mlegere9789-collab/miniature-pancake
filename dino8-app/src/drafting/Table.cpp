#include "drafting/Table.h"

#include <sstream>

#include "util/json_mini.h"

namespace dino8::app::drafting {

namespace {

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else out += c;
    }
  }
  return out;
}

}  // namespace

const std::string& TableSpec::Cell(int r, int c) const {
  static const std::string empty;
  const size_t i = static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c);
  return (r >= 0 && c >= 0 && r < rows && c < cols && i < cells.size()) ? cells[i] : empty;
}

double TableSpec::ColWidth(int c) const {
  if (c >= 0 && c < static_cast<int>(col_widths.size()) && col_widths[static_cast<size_t>(c)] > 0) return col_widths[static_cast<size_t>(c)];
  return 20.0;
}

std::string TableDataJson(const TableSpec& spec) {
  std::ostringstream ss;
  ss << "{\"rows\":" << spec.rows << ",\"cols\":" << spec.cols;
  ss << ",\"rowh\":" << spec.row_height << ",\"texth\":" << spec.text_height;
  ss << ",\"title\":\"" << JsonEscape(spec.title) << "\"";
  ss << ",\"widths\":[";
  for (size_t i = 0; i < spec.col_widths.size(); ++i) ss << (i ? "," : "") << spec.col_widths[i];
  ss << "],\"cells\":[";
  for (size_t i = 0; i < spec.cells.size(); ++i) ss << (i ? "," : "") << "\"" << JsonEscape(spec.cells[i]) << "\"";
  ss << "]}";
  return ss.str();
}

bool ParseTableDataJson(const std::string& json, TableSpec& spec) {
  dino8::json::Value v;
  std::string err;
  if (!dino8::json::Parse(json, v, err) || !v.IsObject()) return false;
  spec.rows = static_cast<int>(v["rows"].number);
  spec.cols = static_cast<int>(v["cols"].number);
  spec.row_height = v["rowh"].number;
  spec.text_height = v["texth"].number;
  spec.title = v["title"].AsString();
  spec.col_widths.clear();
  for (size_t i = 0; i < v["widths"].Size(); ++i) spec.col_widths.push_back(v["widths"][i].number);
  spec.cells.clear();
  for (size_t i = 0; i < v["cells"].Size(); ++i) spec.cells.push_back(v["cells"][i].AsString());
  return spec.rows > 0 && spec.cols > 0;
}

}  // namespace dino8::app::drafting
