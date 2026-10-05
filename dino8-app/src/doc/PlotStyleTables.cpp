#include "doc/PlotStyleTables.h"

#include <algorithm>
#include <sstream>

#include "util/json_mini.h"

namespace dino8::app {

namespace {
constexpr const char* kUserTextKey = "dino8.plot_style_tables";

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

int ByteOf(float c) { return std::clamp(static_cast<int>(c * 255.f + 0.5f), 0, 255); }

std::string Serialize(const std::string& active, const std::vector<PlotStyleTable>& tables) {
  std::ostringstream out;
  out << "{\"active\":\"" << JsonEscape(active) << "\",\"tables\":[";
  for (size_t i = 0; i < tables.size(); ++i) {
    const PlotStyleTable& t = tables[i];
    out << (i ? "," : "") << "{\"name\":\"" << JsonEscape(t.name) << "\",\"entries\":[";
    for (size_t j = 0; j < t.entries.size(); ++j) {
      const PlotStyleEntry& e = t.entries[j];
      out << (j ? "," : "") << "{\"layer\":\"" << JsonEscape(e.layer) << "\",\"color\":" << (e.has_color ? 1 : 0)
          << ",\"r\":" << ByteOf(e.color.r) << ",\"g\":" << ByteOf(e.color.g) << ",\"b\":" << ByteOf(e.color.b)
          << ",\"width\":" << e.width_mm << "}";
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}
}  // namespace

std::vector<PlotStyleTable> LoadPlotStyleTables(const Document& doc) {
  std::vector<PlotStyleTable> out;
  auto it = const_cast<Document&>(doc).UserText().find(kUserTextKey);
  if (it == const_cast<Document&>(doc).UserText().end() || it->second.empty()) return out;
  json::Value root;
  std::string err;
  if (!json::Parse(it->second, root, err) || !root.IsObject()) return out;
  const json::Value& tables = root["tables"];
  if (!tables.IsArray()) return out;
  for (size_t i = 0; i < tables.Size(); ++i) {
    const json::Value& t = tables[i];
    PlotStyleTable table;
    table.name = t["name"].AsString();
    const json::Value& entries = t["entries"];
    for (size_t j = 0; j < entries.Size(); ++j) {
      const json::Value& e = entries[j];
      PlotStyleEntry entry;
      entry.layer = e["layer"].AsString();
      entry.has_color = e["color"].number != 0;
      entry.color = Color::FromBytes(static_cast<int>(e["r"].number), static_cast<int>(e["g"].number), static_cast<int>(e["b"].number));
      entry.width_mm = e["width"].number;
      table.entries.push_back(std::move(entry));
    }
    out.push_back(std::move(table));
  }
  return out;
}

void SavePlotStyleTables(Document& doc, const std::vector<PlotStyleTable>& tables) {
  // Round-tripping must not silently drop which table is active just
  // because the caller only touched the table list - preserve whatever
  // ActivePlotStyleTableName() currently reads before overwriting the
  // same JSON blob it lives in.
  const std::string active = ActivePlotStyleTableName(doc);
  doc.UserText()[kUserTextKey] = Serialize(active, tables);
}

std::string ActivePlotStyleTableName(const Document& doc) {
  auto it = const_cast<Document&>(doc).UserText().find(kUserTextKey);
  if (it == const_cast<Document&>(doc).UserText().end() || it->second.empty()) return "";
  json::Value root;
  std::string err;
  if (!json::Parse(it->second, root, err) || !root.IsObject()) return "";
  return root["active"].AsString();
}

void SetActivePlotStyleTableName(Document& doc, const std::string& name) {
  // Preserve the existing table list the same way SavePlotStyleTables
  // preserves the active name - this only changes which table is active.
  std::vector<PlotStyleTable> tables = LoadPlotStyleTables(doc);
  doc.UserText()[kUserTextKey] = Serialize(name, tables);
}

const PlotStyleTable* FindPlotStyleTable(const std::vector<PlotStyleTable>& tables, const std::string& name) {
  if (name.empty()) return nullptr;
  for (const PlotStyleTable& t : tables) if (t.name == name) return &t;
  return nullptr;
}

const PlotStyleTable* ActivePlotStyleTable(const Document& doc, std::vector<PlotStyleTable>& storage) {
  storage = LoadPlotStyleTables(doc);
  return FindPlotStyleTable(storage, ActivePlotStyleTableName(doc));
}

}  // namespace dino8::app
