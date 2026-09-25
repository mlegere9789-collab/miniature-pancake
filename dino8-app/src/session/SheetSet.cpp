#include "session/SheetSet.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "io/File3dm.h"
#include "io/FileExchange.h"
#include "util/json_mini.h"

namespace dino8::app {

namespace {
namespace fs = std::filesystem;

std::string EscapeJson(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

std::string SanitizeForFilename(const std::string& s) {
  std::string out;
  for (char c : s) {
    out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
  }
  return out.empty() ? "sheet" : out;
}

}  // namespace

bool SaveSheetSet(const std::string& path, const SheetSetFile& set, std::string& error) {
  std::ofstream out(path);
  if (!out) { error = "Could not write " + path; return false; }
  out << "{\n  \"dino8_sheetset\": 1,\n  \"name\": \"" << EscapeJson(set.name) << "\",\n  \"entries\": [\n";
  for (size_t i = 0; i < set.entries.size(); ++i) {
    out << "    {\"file\": \"" << EscapeJson(set.entries[i].file) << "\", \"layout\": \""
        << EscapeJson(set.entries[i].layout) << "\"}" << (i + 1 < set.entries.size() ? "," : "") << "\n";
  }
  out << "  ]\n}\n";
  return static_cast<bool>(out);
}

bool CreateSheetSet(const std::string& path, const std::string& name, std::string& error) {
  SheetSetFile set;
  set.name = name;
  return SaveSheetSet(path, set, error);
}

bool LoadSheetSet(const std::string& path, SheetSetFile& out, std::string& error) {
  std::ifstream in(path);
  if (!in) { error = "Could not open " + path; return false; }
  std::stringstream buf;
  buf << in.rdbuf();
  json::Value root;
  if (!json::Parse(buf.str(), root, error) || !root.IsObject()) {
    if (error.empty()) error = path + " is not a valid sheet set file";
    return false;
  }
  out.name = root["name"].AsString();
  out.entries.clear();
  const json::Value& entries = root["entries"];
  if (entries.IsArray()) {
    for (size_t i = 0; i < entries.Size(); ++i) {
      SheetSetEntry e;
      e.file = entries[i]["file"].AsString();
      e.layout = entries[i]["layout"].AsString();
      if (!e.file.empty()) out.entries.push_back(std::move(e));
    }
  }
  return true;
}

bool AddSheetSetEntry(const std::string& path, const std::string& file, const std::string& layout,
                       std::string& error) {
  SheetSetFile set;
  if (!LoadSheetSet(path, set, error)) return false;
  for (const SheetSetEntry& e : set.entries) {
    if (e.file == file && e.layout == layout) return true;  // already present
  }
  set.entries.push_back({file, layout});
  return SaveSheetSet(path, set, error);
}

std::vector<SheetPlotResult> PlotSheetSet(const SheetSetFile& set, const std::string& out_dir) {
  std::vector<SheetPlotResult> results;
  results.reserve(set.entries.size());
  std::error_code ec;
  fs::create_directories(out_dir, ec);

  for (const SheetSetEntry& entry : set.entries) {
    SheetPlotResult r;
    r.file = entry.file;
    r.layout = entry.layout;

    Document ref;  // throwaway, read-only load - never touches the caller's own document
    std::string err;
    if (!Load3dm(ref, entry.file, err)) {
      r.error = "could not open " + entry.file + (err.empty() ? "" : (": " + err));
      results.push_back(std::move(r));
      continue;
    }
    Layout* layout = entry.layout.empty() ? nullptr : ref.FindLayout(entry.layout);
    if (!layout) {
      r.error = "layout '" + entry.layout + "' not found in " + entry.file;
      results.push_back(std::move(r));
      continue;
    }

    DrawingOptions opts;
    opts.page_width_mm = layout->width_mm;
    opts.page_height_mm = layout->height_mm;

    const std::string stem = fs::path(entry.file).stem().string();
    const std::string out_path =
        (fs::path(out_dir) / (SanitizeForFilename(stem) + "__" + SanitizeForFilename(entry.layout) + ".pdf")).string();
    r.output_path = out_path;
    if (!ExportPdf(ref, /*view=*/nullptr, out_path, /*selected_only=*/false, opts, err)) {
      r.error = err.empty() ? "PDF export failed" : err;
      results.push_back(std::move(r));
      continue;
    }
    r.ok = true;
    results.push_back(std::move(r));
  }
  return results;
}

}  // namespace dino8::app
