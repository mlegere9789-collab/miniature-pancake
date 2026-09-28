#include "platform/AccessibilityTree.h"

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
                                    const std::string& command_input, const std::deque<std::string>& history) {
  AccessibleNode command_line;
  command_line.name = "Command Line";
  command_line.role = AccessibleRole::Log;
  command_line.text = BuildCommandLineText(prompt, command_input, history);

  AccessibleNode root;
  root.name = app_name;
  root.role = AccessibleRole::Application;
  root.children.push_back(std::move(command_line));
  return root;
}

}  // namespace dino8::platform
