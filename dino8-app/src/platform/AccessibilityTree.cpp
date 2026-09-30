#include "platform/AccessibilityTree.h"

#include <cstdio>

namespace dino8::platform {

std::string BuildCommandLineText(const std::string& prompt, const std::string& command_input,
                                  const std::deque<std::string>& history) {
  std::string text;
  for (const std::string& line : history) {
    text += line;
    text += '\n';
  }
  text += prompt;
  text += command_input;
  return text;
}

AccessibleNode BuildAccessibleTree(const std::string& app_name, const std::string& prompt,
                                    const std::string& command_input, const std::deque<std::string>& history,
                                    std::vector<AccessibleNode> extra_top_level_children) {
  AccessibleNode command_line;
  command_line.name = "Command Line";
  command_line.role = AccessibleRole::Log;
  command_line.description = "Command line input and the command-history log above it";
  command_line.text = BuildCommandLineText(prompt, command_input, history);

  AccessibleNode root;
  root.name = app_name;
  root.role = AccessibleRole::Application;
  root.children.push_back(std::move(command_line));
  for (AccessibleNode& n : extra_top_level_children) root.children.push_back(std::move(n));
  return root;
}

MenuTreeBuilder::MenuTreeBuilder() {
  AccessibleNode root;
  root.name = "Menu Bar";
  root.role = AccessibleRole::MenuBar;
  stack_.push_back(std::move(root));
}

void MenuTreeBuilder::OpenMenu(const std::string& label) {
  AccessibleNode n;
  n.name = label;
  n.role = AccessibleRole::Menu;
  stack_.push_back(std::move(n));
}

void MenuTreeBuilder::CloseMenu() {
  AccessibleNode n = std::move(stack_.back());
  stack_.pop_back();
  stack_.back().children.push_back(std::move(n));
}

void MenuTreeBuilder::LeafMenu(const std::string& label) {
  AccessibleNode n;
  n.name = label;
  n.role = AccessibleRole::Menu;
  stack_.back().children.push_back(std::move(n));
}

void MenuTreeBuilder::Item(const std::string& label, const std::string& shortcut) {
  AccessibleNode n;
  n.name = shortcut.empty() ? label : label + " (" + shortcut + ")";
  n.role = AccessibleRole::MenuItem;
  stack_.back().children.push_back(std::move(n));
}

namespace {
std::string YesNo(bool b) { return b ? "yes" : "no"; }
}  // namespace

AccessibleNode BuildLayersPanelNode(const std::vector<LayerSummary>& layers) {
  AccessibleNode list;
  list.name = "Layers";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu layers", layers.size());
  list.description = count_buf;

  for (const LayerSummary& l : layers) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = l.name + (l.current ? ", current layer" : "") + ", visible " + YesNo(l.visible) + ", locked " +
                YesNo(l.locked) + ", " + std::to_string(l.object_count) +
                (l.object_count == 1 ? " object" : " objects");
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildPropertiesPanelNode(const std::string& heading, const std::vector<PropertyEntry>& entries) {
  AccessibleNode list;
  list.name = "Properties";
  list.role = AccessibleRole::List;
  list.description = heading;

  for (const PropertyEntry& e : entries) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = e.label + ": " + e.value;
    if (e.editable) item.description = "Editable: type a new " + e.label + " to change it";
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildCommandOptionsNode(const std::vector<CommandOptionSummary>& options) {
  AccessibleNode list;
  list.name = "Command Options";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu options", options.size());
  list.description = count_buf;

  for (const CommandOptionSummary& o : options) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = o.value.empty() ? o.name : o.name + "=" + o.value;
    if (o.toggle) {
      item.description = "Click to toggle (or type " + o.name + ")";
    } else if (!o.choices.empty()) {
      std::string choices;
      for (size_t i = 0; i < o.choices.size(); ++i) {
        if (i) choices += ", ";
        choices += o.choices[i];
      }
      item.description = "Click to cycle through: " + choices + " (or type " + o.name + ")";
    } else if (o.numeric) {
      item.description = "Click, then type a new value (or type " + o.name + ")";
    } else {
      item.description = "Option: " + o.name + " (or type its name)";
    }
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildViewportsPanelNode(const std::vector<ViewportSummary>& viewports) {
  AccessibleNode list;
  list.name = "Viewports";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu viewports", viewports.size());
  list.description = count_buf;

  for (const ViewportSummary& v : viewports) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = v.name + (v.active ? ", active" : "") + (v.maximized ? ", maximized" : "") + ", display mode " +
                v.display_mode;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildActivityLogNode(const std::vector<ActivityLogSummary>& entries) {
  AccessibleNode list;
  list.name = "Activity Log";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu entries", entries.size());
  list.description = count_buf;

  for (const ActivityLogSummary& e : entries) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = e.timestamp_utc + " " + e.label + ": " + e.summary;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildNamedViewsNode(const std::vector<NamedViewSummary>& views) {
  AccessibleNode list;
  list.name = "Named Views";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu named views", views.size());
  list.description = count_buf;

  for (const NamedViewSummary& v : views) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = v.name;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildNamedCPlanesNode(const std::vector<NamedCPlaneSummary>& cplanes) {
  AccessibleNode list;
  list.name = "Named CPlanes";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu named cplanes", cplanes.size());
  list.description = count_buf;

  for (const NamedCPlaneSummary& c : cplanes) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = c.name;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildLinetypesNode(const std::vector<LinetypeSummary>& linetypes) {
  AccessibleNode list;
  list.name = "Linetypes";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu linetypes", linetypes.size());
  list.description = count_buf;

  for (const LinetypeSummary& lt : linetypes) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = lt.name;
    item.description = "Pattern: " + lt.pattern_text;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildMaterialsPanelNode(const std::vector<MaterialSummary>& materials) {
  AccessibleNode list;
  list.name = "Materials";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu materials", materials.size());
  list.description = count_buf;

  for (const MaterialSummary& m : materials) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = m.name;
    item.description = "Colour: " + m.diffuse_text;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildClippingPlanesPanelNode(const std::vector<ClippingPlaneSummary>& planes) {
  AccessibleNode list;
  list.name = "Clipping Planes";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu clipping planes", planes.size());
  list.description = count_buf;

  for (const ClippingPlaneSummary& p : planes) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = p.name + (p.enabled ? ", on" : ", off");
    item.description = p.clips_every_viewport
                            ? "Clips every viewport"
                            : "Clips " + std::to_string(p.clipped_viewport_count) +
                                  (p.clipped_viewport_count == 1 ? " viewport" : " viewports");
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildLayoutsPanelNode(const std::vector<LayoutSummary>& layouts) {
  AccessibleNode list;
  list.name = "Layouts";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu layouts", layouts.size());
  list.description = count_buf;

  for (const LayoutSummary& l : layouts) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = l.name + (l.active ? ", active" : "");
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildBlockManagerNode(const std::vector<BlockSummary>& blocks) {
  AccessibleNode list;
  list.name = "Block Manager";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu block definitions", blocks.size());
  list.description = count_buf;

  for (const BlockSummary& b : blocks) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = b.name;
    item.description = std::to_string(b.object_count) + (b.object_count == 1 ? " object, " : " objects, ") +
                        std::to_string(b.instance_count) + (b.instance_count == 1 ? " instance" : " instances");
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildLayerStateManagerNode(const std::vector<LayerStateSummary>& states) {
  AccessibleNode list;
  list.name = "Layer State Manager";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu layer states", states.size());
  list.description = count_buf;

  for (const LayerStateSummary& s : states) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = s.name;
    item.description = std::to_string(s.layer_count) + (s.layer_count == 1 ? " layer" : " layers");
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildDocumentUserTextNode(const std::vector<DocumentUserTextSummary>& entries) {
  AccessibleNode list;
  list.name = "Document User Text";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu document user text entries", entries.size());
  list.description = count_buf;

  for (const DocumentUserTextSummary& e : entries) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = e.key;
    item.description = e.value;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildLightsPanelNode(const std::vector<LightSummary>& lights) {
  AccessibleNode list;
  list.name = "Lights";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu document lights", lights.size());
  list.description = count_buf;

  for (const LightSummary& l : lights) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = l.name + " (" + l.type_text + "), " + (l.enabled ? "on" : "off");
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildAnnotationStylesNode(const std::vector<AnnotationStyleSummary>& styles) {
  AccessibleNode list;
  list.name = "Annotation Styles";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu annotation styles", styles.size());
  list.description = count_buf;

  for (const AnnotationStyleSummary& s : styles) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = s.name + (s.current ? ", current" : "");
    item.description = "Text height: " + s.text_height_text + "; Arrow size: " + s.arrow_size_text +
                        "; Font: " + s.font_text;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildDocumentNotesNode(const std::string& notes) {
  AccessibleNode node;
  node.name = "Document Notes";
  node.role = AccessibleRole::Log;
  node.description = "The document's saved Notes text";
  node.text = notes;
  return node;
}

namespace {
// Shared by BuildEnvironmentsPanelNode/BuildDocumentPropertiesNode/
// BuildDisplayPanelNode: each is a List of plain "Label: value" facts with
// no count Description (unlike the row-of-entities lists above), just under
// its own accessible name - the same shape BuildUndoRedoHistoryNode shares
// for Undo History/Redo History.
AccessibleNode BuildLabelValueListNode(const std::string& list_name, const std::vector<PropertyEntry>& entries) {
  AccessibleNode list;
  list.name = list_name;
  list.role = AccessibleRole::List;

  for (const PropertyEntry& e : entries) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = e.label + ": " + e.value;
    list.children.push_back(std::move(item));
  }
  return list;
}
}  // namespace

AccessibleNode BuildEnvironmentsPanelNode(const std::vector<PropertyEntry>& entries) {
  return BuildLabelValueListNode("Environments", entries);
}

AccessibleNode BuildAuditResultsNode(const std::vector<AuditIssueSummary>& issues) {
  AccessibleNode list;
  list.name = "Audit Results";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu invalid object(s)", issues.size());
  list.description = count_buf;

  for (const AuditIssueSummary& issue : issues) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = "Object " + std::to_string(issue.id) + " (" + issue.type + ")";
    item.description = issue.description;
    list.children.push_back(std::move(item));
  }
  return list;
}

namespace {
AccessibleNode BuildUndoRedoHistoryNode(const std::string& list_name, const std::vector<std::string>& labels) {
  AccessibleNode list;
  list.name = list_name;
  list.role = AccessibleRole::List;

  for (size_t i = 0; i < labels.size(); ++i) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = std::to_string(i + 1) + ". " + labels[i];
    list.children.push_back(std::move(item));
  }
  return list;
}
}  // namespace

AccessibleNode BuildUndoHistoryNode(const std::vector<std::string>& labels) {
  return BuildUndoRedoHistoryNode("Undo History", labels);
}

AccessibleNode BuildRedoHistoryNode(const std::vector<std::string>& labels) {
  return BuildUndoRedoHistoryNode("Redo History", labels);
}

AccessibleNode BuildHatchPatternsNode(const std::vector<HatchPatternSummary>& patterns) {
  AccessibleNode list;
  list.name = "Hatch Patterns";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu hatch patterns", patterns.size());
  list.description = count_buf;

  for (const HatchPatternSummary& p : patterns) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = p.name;
    item.description = p.description;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildPluginsNode(const std::vector<PluginSummary>& plugins) {
  AccessibleNode list;
  list.name = "Plug-ins";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu plug-in(s)", plugins.size());
  list.description = count_buf;

  for (const PluginSummary& p : plugins) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = p.name + " " + p.version + ", " + (p.loaded_ok ? "Loaded" : "Error");
    item.description = std::to_string(p.command_count) + (p.command_count == 1 ? " command, " : " commands, ") +
                        std::to_string(p.flow_node_count) +
                        (p.flow_node_count == 1 ? " flow node" : " flow nodes");
    if (!p.loaded_ok) item.description += "; " + p.error;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildCommandListNode(const std::vector<CommandListEntrySummary>& commands) {
  AccessibleNode list;
  list.name = "Command List";
  list.role = AccessibleRole::List;

  size_t n_impl = 0, n_part = 0, n_plan = 0;
  for (const CommandListEntrySummary& c : commands) {
    if (c.status_text == "Implemented") ++n_impl;
    else if (c.status_text == "Partial") ++n_part;
    else if (c.status_text == "Planned") ++n_plan;
  }
  char count_buf[160];
  std::snprintf(count_buf, sizeof(count_buf), "%zu commands in the Rhino 8 reference: %zu implemented, %zu partial, %zu planned (help only)",
                commands.size(), n_impl, n_part, n_plan);
  list.description = count_buf;

  for (const CommandListEntrySummary& c : commands) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = c.name;
    item.description = c.status_text;
    if (!c.description.empty()) item.description += ": " + c.description;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildCommandAliasesNode(const std::vector<CommandAliasSummary>& aliases) {
  AccessibleNode list;
  list.name = "Command Aliases";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu command aliases", aliases.size());
  list.description = count_buf;

  for (const CommandAliasSummary& a : aliases) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = a.alias;
    item.description = a.command;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildKeyboardShortcutsNode(const std::vector<KeyboardShortcutSummary>& shortcuts) {
  AccessibleNode list;
  list.name = "Keyboard Shortcuts";
  list.role = AccessibleRole::List;
  char count_buf[32];
  std::snprintf(count_buf, sizeof(count_buf), "%zu keyboard shortcuts", shortcuts.size());
  list.description = count_buf;

  for (const KeyboardShortcutSummary& s : shortcuts) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = s.combo_text;
    item.description = s.command;
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildDocumentPropertiesNode(const std::vector<PropertyEntry>& entries) {
  return BuildLabelValueListNode("Document Properties", entries);
}

AccessibleNode BuildTexturesPanelNode(const std::vector<TextureSummary>& textures) {
  AccessibleNode list;
  list.name = "Textures";
  list.role = AccessibleRole::List;
  char count_buf[48];
  std::snprintf(count_buf, sizeof(count_buf), "%zu material(s) carry a texture", textures.size());
  list.description = count_buf;

  for (const TextureSummary& t : textures) {
    AccessibleNode item;
    item.role = AccessibleRole::ListItem;
    item.name = t.name;
    item.description = t.mapping_text + " mapping, " + (t.found ? "found" : "missing");
    list.children.push_back(std::move(item));
  }
  return list;
}

AccessibleNode BuildDisplayPanelNode(const std::vector<PropertyEntry>& entries) {
  return BuildLabelValueListNode("Display", entries);
}

}  // namespace dino8::platform
