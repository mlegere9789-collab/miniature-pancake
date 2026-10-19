// 3Dconnexion / SpaceMouse (SpaceNavigator, SpaceMouse Compact/Wireless/Pro/
// Enterprise) 6-DOF input support, built without any external library:
//   Linux:   raw HID (/dev/hidraw*) for vendor 0x046d / 0x256f devices, with
//            an evdev (/dev/input/event*) fallback for kernels that only
//            expose the device as a generic joystick/HID input node.
//   Windows: Raw Input (RegisterRawInputDevices, usage page 1 usage 8) via a
//            private message-only window owned by the polling thread.
//   macOS:   IOKit's IOHIDManager, matched by vendor id.
//   Any platform: a "File" protocol that replays 6-DOF deltas from a text
//            file, one sample per line, so headless tests can prove the
//            camera (or selection) actually moves without real hardware.
//
// A background thread owns the device handle and only ever pushes parsed
// samples into a mutex-protected queue; Application::Frame() (via
// input::SpaceMouseFrame) drains that queue on the main thread and is the
// only place that touches Viewport/Document state, so there is no need for
// the device thread to know anything about the rest of the app.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dino8::app { class Application; class CommandEngine; }

namespace dino8::input {

// One 6-DOF report: translation (tx,ty,tz) and rotation (rx,ry,rz), each
// normalized to roughly [-1, 1] (a report at the physical limit of the
// puck's travel). Buttons newly pressed since the previous report (device
// button indices, 0-based) ride along on whichever report follows them.
struct SixDofSample {
  double tx = 0, ty = 0, tz = 0;
  double rx = 0, ry = 0, rz = 0;
  std::vector<int> buttons_pressed;
};

enum class SpaceMouseMode { Camera, Fly, Object };

// Persisted in <config dir>/spacemouse.json (see Load/Save below) -
// deliberately its own small file rather than another dozen fields bolted
// onto Settings.cpp/json, so this feature's on-disk footprint stays out of
// everything else's serialization code.
struct SpaceMouseOptions {
  bool enabled = true;
  // Auto | HidRaw | Evdev | RawInput | IOKit | File
  std::string protocol = "Auto";
  std::string file_path;  // Protocol=File: text file of "tx ty tz rx ry rz" lines

  double translation_sensitivity = 1.0;
  double rotation_sensitivity = 1.0;
  bool invert_tx = false, invert_ty = false, invert_tz = false;
  bool invert_rx = false, invert_ry = false, invert_rz = false;
  double dead_zone = 0.02;       // |axis| below this is treated as zero
  bool dominant_axis = false;    // only the single largest-magnitude axis moves per sample
  SpaceMouseMode mode = SpaceMouseMode::Camera;

  // Button index -> command name run on press (Fit/Top/Right/Front/Menu by
  // default; see DefaultButtonCommands()).
  std::map<int, std::string> button_commands;
};

const char* ModeName(SpaceMouseMode m);
bool ParseMode(const std::string& s, SpaceMouseMode& out);
std::map<int, std::string> DefaultButtonCommands();

class SpaceMouseDevice {
 public:
  SpaceMouseDevice();
  ~SpaceMouseDevice();

  SpaceMouseDevice(const SpaceMouseDevice&) = delete;
  SpaceMouseDevice& operator=(const SpaceMouseDevice&) = delete;

  // (Re)starts the background thread with the current Options(). Safe to
  // call again after changing options (stops the old thread first).
  bool Start(std::string& error);
  void Stop();
  bool Running() const { return running_.load(); }
  bool Connected() const { return connected_.load(); }
  std::string StatusText() const;
  const std::string& DeviceName() const { return device_name_; }

  SpaceMouseOptions& Options() { return options_; }
  const SpaceMouseOptions& Options() const { return options_; }

  // Drains every sample queued since the last call, already summed into
  // one delta (good enough for a per-frame camera/object update - a
  // SpaceMouse reports at ~50-370 Hz, far faster than the UI frame rate).
  bool DrainDelta(SixDofSample& out);

  bool LoadPersisted();  // <config>/spacemouse.json, if present
  bool SavePersisted() const;

  // Called from the platform-specific static callbacks (Windows WndProc,
  // macOS IOKit callbacks) which cannot be private member functions.
  void PushSampleWin(const SixDofSample& s);
  void PushSampleMac(const SixDofSample& s);
  void SetConnectedMac(const std::string& name);

 private:
  void ThreadMain();
  void RunFile();
#if defined(_WIN32)
  void RunRawInput();
#elif defined(__APPLE__)
  void RunIOKit();
#else
  void RunHidRaw();
  void RunEvdev();
#endif
  void PushSample(const SixDofSample& s);

  SpaceMouseOptions options_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> connected_{false};
  std::string device_name_;
  mutable std::mutex mutex_;
  std::deque<SixDofSample> queue_;
  mutable std::mutex status_mutex_;
  std::string status_;
};

// Process-wide instance: the app has exactly one physical (or file-backed)
// SpaceMouse, so a singleton keeps Application.h free of a dedicated field.
SpaceMouseDevice& Device();

// Called once from Application::Init / Shutdown and every frame from
// Application::Frame; all three are the only integration points into the
// rest of the app (see Application.cpp).
void Init(dino8::app::Application& app);
void Shutdown();
void Frame(dino8::app::Application& app);

// Draws the Options > SpaceMouse page (its own ImGui window, opened by the
// SpaceMouse / 3DconnexionOptions commands) - kept here rather than adding
// a tab to Panels.cpp's DrawOptionsWindow so this feature owns its own UI.
void DrawOptionsPanel(dino8::app::Application& app);
void ShowOptionsPanel();
bool OptionsPanelVisible();

void RegisterSpaceMouseCommands(dino8::app::CommandEngine& e);

}  // namespace dino8::input
