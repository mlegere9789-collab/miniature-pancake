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

}  // namespace dino8::platform
