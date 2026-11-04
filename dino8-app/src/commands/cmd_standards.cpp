// CAD Standards Checker: a subset of AutoCAD's STANDARDS/CHECKSTANDARDS.
//
// A CAD manager writes a plain, human-editable JSON "standards file" naming
// the layers a drawing is allowed to use (with each one's expected colour
// and linetype) and the annotation styles it is allowed to use. `Standards`
// links the current document to that file (the path is stored in document
// user text, the same SetDocumentUserText/Document::UserText() idiom used
// by sketch/Constraints.h and arch/ArchComponents.h). `CheckStandards`
// re-reads the file and walks every layer and every annotation style
// actually present/used in the document, reporting each drift as one line:
// a layer name the file doesn't define, or a defined layer whose colour or
// linetype doesn't match, or an annotation style in use that isn't listed.
//
// Standards-file schema (all fields optional beyond a layer's "name"):
//   {
//     "layers": [ {"name": "Walls", "color": "0,0,0", "linetype": "Continuous"}, ... ],
//     "text_styles": ["Standard", ...],
//     "dim_styles": ["Standard", ...]
//   }
// Dino 8 does not keep separate text-style and dimension-style tables the
// way AutoCAD does - every annotation (text or dimension) is tagged with
// one shared AnnotationStyle name (see Document.h's AnnotationStyle and
// the "Style" user-text tag set in commands/annotate_common.h). So
// "text_styles" and "dim_styles" are both folded into a single allowed-name
// set here; a manager can list styles under either key (or both) and this
// checker treats them the same. Likewise Dino 8 layers carry no per-layer
// lineweight (see the Layer struct in Document.h), so lineweight is not
// part of this schema or this check.
//
// Explicitly out of scope for this increment: AutoCAD's interactive
// "Standards Violation" fix-it dialog, which lets a user click a flagged
// object and apply the standard's layer/style/colour to it in place. That
// is a materially larger UI feature (a dialog wired to live document
// edits); this increment is a text/console report only, in the same style
// Purge and SetDocumentUserText already print in.
#include "commands/cmd_common.h"
#include "util/json_mini.h"

#include <fstream>
#include <set>
#include <sstream>

namespace dino8::app {

namespace {

constexpr const char* kStandardsFileKey = "dino8.standards_file";

struct LayerStandard {
  std::string name;
  bool has_color = false;
  Color color;
  std::string linetype;  // empty: linetype not checked for this layer
};

struct Standards {
  std::vector<LayerStandard> layers;
  std::set<std::string> styles;  // allowed text/dimension style names
};

// Parses "r,g,b" (each 0-255); returns false on anything else.
bool ParseColorSpec(const std::string& s, Color& out) {
  int r = 0, g = 0, b = 0;
  char c1 = 0, c2 = 0;
  std::istringstream iss(s);
  if (!(iss >> r >> c1 >> g >> c2 >> b) || c1 != ',' || c2 != ',') return false;
  out = Color::FromBytes(r, g, b);
  return true;
}

int ColorByte(float channel) { return static_cast<int>(channel * 255.f + 0.5f); }

bool SameColor(const Color& a, const Color& b) {
  return ColorByte(a.r) == ColorByte(b.r) && ColorByte(a.g) == ColorByte(b.g) && ColorByte(a.b) == ColorByte(b.b);
}

std::string ColorText(const Color& c) {
  return std::to_string(ColorByte(c.r)) + "," + std::to_string(ColorByte(c.g)) + "," + std::to_string(ColorByte(c.b));
}

const LayerStandard* FindLayerStandard(const Standards& s, const std::string& name) {
  for (const LayerStandard& l : s.layers) if (l.name == name) return &l;
  return nullptr;
}

// Reads and parses the standards JSON file at `path`. Returns false (with
// `error` describing why) if it cannot be opened or isn't a JSON object -
// matches i18n::LoadFile's "read the whole file, parse with json_mini"
// shape (src/i18n/I18n.cpp).
bool LoadStandardsFile(const std::string& path, Standards& out, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { error = "cannot open '" + path + "'"; return false; }
  std::stringstream buf;
  buf << in.rdbuf();
  json::Value root;
  if (!json::Parse(buf.str(), root, error) || !root.IsObject()) {
    if (error.empty()) error = "'" + path + "' is not a JSON object";
    return false;
  }

  const json::Value& layers = root["layers"];
  if (layers.IsArray()) {
    for (const json::Value& l : layers.array) {
      if (!l.IsObject()) continue;
      LayerStandard ls;
      ls.name = l["name"].AsString();
      if (ls.name.empty()) continue;
      if (l["color"].IsString()) ls.has_color = ParseColorSpec(l["color"].string, ls.color);
      if (l["linetype"].IsString()) ls.linetype = l["linetype"].string;
      out.layers.push_back(std::move(ls));
    }
  }
  auto add_style_names = [&](const char* key) {
    const json::Value& arr = root[key];
    if (!arr.IsArray()) return;
    for (const json::Value& v : arr.array) {
      if (v.IsString()) out.styles.insert(v.string);
      else if (v.IsObject() && v["name"].IsString()) out.styles.insert(v["name"].string);
    }
  };
  add_style_names("text_styles");
  add_style_names("dim_styles");
  return true;
}

}  // namespace

void RegisterStandardsCommands(CommandEngine& e) {
  Reg(e, "Standards", Immediate([](CommandContext& ctx) {
        if (auto p = ctx.Engine().TakePendingInput()) {
          std::ifstream check(*p, std::ios::binary);
          if (!check) { ctx.Warn("Standards: cannot open '" + *p + "' - the file is not linked"); return; }
          ctx.Doc().UserText()[kStandardsFileKey] = *p;
          ctx.Doc().Touch();
          ctx.Print("Standards: document linked to '" + *p + "'. Run CheckStandards to audit it.");
          return;
        }
        auto it = ctx.Doc().UserText().find(kStandardsFileKey);
        if (it == ctx.Doc().UserText().end())
          ctx.Print("Standards: no standards file linked. Usage: Standards <path-to-json>");
        else
          ctx.Print("Standards: linked to '" + it->second + "'");
      }),
      CommandStatus::Implemented,
      "Links the document to a CAD standards JSON file (see cmd_standards.cpp) for CheckStandards to audit against.");

  Reg(e, "CheckStandards", Immediate([](CommandContext& ctx) {
        auto it = ctx.Doc().UserText().find(kStandardsFileKey);
        if (it == ctx.Doc().UserText().end()) { ctx.Warn("CheckStandards: no standards file linked - run Standards <path> first"); return; }

        Standards standards;
        std::string error;
        if (!LoadStandardsFile(it->second, standards, error)) { ctx.Warn("CheckStandards: " + error); return; }

        std::vector<std::string> violations;

        // Every layer in the document's layer table is audited, whether or
        // not any object currently sits on it - a stray or misnamed layer
        // is itself a standards drift, the same way AutoCAD's checker
        // audits the whole layer table rather than only layers in use.
        for (const Layer& layer : ctx.Doc().Layers()) {
          const LayerStandard* std_layer = FindLayerStandard(standards, layer.name);
          if (!std_layer) { violations.push_back("layer '" + layer.name + "' is not defined in the standards file"); continue; }
          if (std_layer->has_color && !SameColor(layer.color, std_layer->color))
            violations.push_back("layer '" + layer.name + "' color " + ColorText(layer.color) + " does not match the standard " + ColorText(std_layer->color));
          if (!std_layer->linetype.empty() && layer.linetype != std_layer->linetype)
            violations.push_back("layer '" + layer.name + "' linetype '" + layer.linetype + "' does not match the standard '" + std_layer->linetype + "'");
        }

        // Text/dimension styles: only ones actually used by an annotation
        // object are checked (Dino 8 keeps no fixed text-style/dim-style
        // table beyond AnnotationStyles - see the comment atop this file).
        // Every annotation group tags its member objects "Annotation" (the
        // kind) and "Style" (the style name) in user text - see
        // commands/annotate_common.h's TagAnnotation.
        std::set<std::string> used_styles;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          if (!o.user_text.count("Annotation")) continue;
          auto style_it = o.user_text.find("Style");
          if (style_it != o.user_text.end() && !style_it->second.empty()) used_styles.insert(style_it->second);
        }
        for (const std::string& name : used_styles) {
          if (!standards.styles.count(name))
            violations.push_back("text/dimension style '" + name + "' is used in the drawing but is not in the standards file");
        }

        if (violations.empty()) {
          ctx.Print("CheckStandards: 0 violations - the drawing matches '" + it->second + "'");
        } else {
          ctx.Print("CheckStandards found " + std::to_string(violations.size()) + " violation(s) against '" + it->second + "':");
          for (const std::string& v : violations) ctx.Print("  - " + v);
        }
      }),
      CommandStatus::Implemented,
      "Audits every layer and used annotation style against the linked standards file and prints a violation report (no interactive fix-it dialog - see cmd_standards.cpp).");
}

}  // namespace dino8::app
