#pragma once
// Line-based text buffer for the script / text editor rows (bpy.ops.text.*).
//
// Header-only. Lines are stored without terminators; the selection is a pair of line indices (inclusive), which
// is all the line operations below need. Indent width is 4 spaces and indentation is made of spaces
// (`tabs_as_spaces`), or one tab per level when that is off.
//
// Each operation is defined by what it does to the lines, so it can be checked as a permutation or an inverse
// pair: Indent / Unindent, Comment toggle (a Python comment line is a no-op to the interpreter), Duplicate Line
// and Move Lines (the multiset of lines changes only as stated).
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace dino8 {
namespace sim {

class TextBuffer {
 public:
  std::vector<std::string> lines;
  int cursor_line = 0;
  int sel_first = 0, sel_last = 0;  // inclusive line range acted on by block operations
  bool tabs_as_spaces = true;
  int tab_width = 4;

  TextBuffer() : lines(1) {}
  explicit TextBuffer(std::vector<std::string> l) : lines(std::move(l)) {
    if (lines.empty()) lines.resize(1);
  }
  std::string Joined() const {
    std::string s;
    for (std::size_t i = 0; i < lines.size(); ++i) { if (i) s += '\n'; s += lines[i]; }
    return s;
  }
  void SelectLines(int a, int b) {
    sel_first = std::max(0, std::min(a, b));
    sel_last = std::min(static_cast<int>(lines.size()) - 1, std::max(a, b));
  }

  // Duplicate Line: the cursor line is copied directly below it and the cursor moves onto the copy.
  void DuplicateLine() {
    const std::size_t i = static_cast<std::size_t>(cursor_line);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i) + 1, lines[i]);
    ++cursor_line;
    sel_first = sel_last = cursor_line;
  }
  std::string IndentUnit() const { return tabs_as_spaces ? std::string(static_cast<std::size_t>(tab_width), ' ') : "\t"; }
  // Indent: one indentation unit is put in front of every line of the selection (empty lines included).
  void Indent() {
    const std::string u = IndentUnit();
    for (int i = sel_first; i <= sel_last; ++i) lines[static_cast<std::size_t>(i)].insert(0, u);
  }
  // Unindent: removes up to one unit of leading whitespace from every line of the selection: a leading tab, or as
  // many leading spaces as there are, at most tab_width.
  void Unindent() {
    for (int i = sel_first; i <= sel_last; ++i) {
      std::string& l = lines[static_cast<std::size_t>(i)];
      if (!l.empty() && l[0] == '\t') { l.erase(0, 1); continue; }
      std::size_t n = 0;
      while (n < l.size() && n < static_cast<std::size_t>(tab_width) && l[n] == ' ') ++n;
      l.erase(0, n);
    }
  }
  // Toggle Comments: when every selected line already starts with '#', that '#' is removed from each; otherwise a
  // '#' is put in front of each selected line. Applying it twice restores the text.
  void CommentToggle() {
    bool all = true;
    for (int i = sel_first; i <= sel_last; ++i) {
      const std::string& l = lines[static_cast<std::size_t>(i)];
      all = all && !l.empty() && l[0] == '#';
    }
    for (int i = sel_first; i <= sel_last; ++i) {
      std::string& l = lines[static_cast<std::size_t>(i)];
      if (all) l.erase(0, 1); else l.insert(0, "#");
    }
  }
  // Move Lines: the selected block swaps places with the line above (up) or below (down); the selection and the
  // cursor follow. At the first / last line nothing happens. Returns whether it moved.
  bool MoveLines(bool up) {
    const int n = static_cast<int>(lines.size());
    if (up) {
      if (sel_first <= 0) return false;
      std::rotate(lines.begin() + (sel_first - 1), lines.begin() + sel_first, lines.begin() + sel_last + 1);
      --sel_first; --sel_last; --cursor_line;
    } else {
      if (sel_last >= n - 1) return false;
      std::rotate(lines.begin() + sel_first, lines.begin() + sel_last + 1, lines.begin() + sel_last + 2);
      ++sel_first; ++sel_last; ++cursor_line;
    }
    return true;
  }
};

}  // namespace sim
}  // namespace dino8
