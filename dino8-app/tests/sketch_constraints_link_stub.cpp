// Link-only stub for dino8_test_sketch_constraints (see CMakeLists.txt's
// own comment on that target). sketch/Constraints.cpp's AutoResolveFrame()
// - not exercised by this test target at all, only compiled alongside the
// SolveAll()/LoadConstraints() functions this test actually calls, since
// all three live in the same source file - calls
// dino8::app::Application::ActiveViewport(), whose real definition lives
// in src/app/Application.cpp. That file is deliberately NOT one of this
// target's sources (pulling it in would pull in the entire GL/ImGui app
// just to satisfy a function this binary never calls), so the linker
// needs a definition for this one symbol from somewhere.
//
// AutoResolveFrame's own `if (!vp) return;` guard means a null return here
// is never actually exercised by anything this test calls - this exists
// purely so the linker has something to resolve against, not to provide
// real behavior. Safe only as long as this exact target never also links
// the real Application.cpp (which would then collide with this one) -
// see CMakeLists.txt's own dino8_test_sketch_constraints target.
#include "app/Application.h"

namespace dino8::app {
Viewport* Application::ActiveViewport() { return nullptr; }
}  // namespace dino8::app
