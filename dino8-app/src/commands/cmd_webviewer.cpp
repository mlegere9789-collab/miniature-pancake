// ExportWebViewer: the interactive surface for io/WebViewer.h's
// self-contained HTML/WebGL model viewer export - PARITY_MAP.md's "Cloud
// model viewer / app builder (ShapeDiver equivalent)" item.
#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "io/WebViewer.h"

namespace dino8::app {

namespace {

void ExportWebViewerCmd(CommandContext& ctx) {
  const auto opts = TakeOptionTokens(ctx);
  const std::string path = OptionOr(opts, "path");
  if (path.empty()) {
    ctx.Warn("ExportWebViewer Path=<file.html> [Selected=Yes]");
    return;
  }
  const bool selected_only = ToLower(OptionOr(opts, "selected", "no")) == "yes";
  std::string error;
  if (!ExportWebViewer(ctx.Doc(), path, selected_only, error)) {
    ctx.Warn("ExportWebViewer: " + error);
    return;
  }
  ctx.Print("ExportWebViewer: wrote " + path + " - open it in any browser (no server or install needed)");
}

}  // namespace

void RegisterWebViewerCommands(CommandEngine& e) {
  Reg(e, "ExportWebViewer", Immediate(ExportWebViewerCmd), CommandStatus::Implemented,
      "Exports the document (or, with Selected=Yes, just the current selection) as one self-contained .html file "
      "with an embedded WebGL viewer (orbit/zoom) - no server, install or network access needed to view it. "
      "Path=<file.html> Selected=<Yes/No, default No>. See io/WebViewer.h for the exact scope.");
}

}  // namespace dino8::app
