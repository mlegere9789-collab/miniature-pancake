#include "input/SpaceMouse.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "app/Application.h"
#include "app/Settings.h"
#include "commands/Command.h"
#include "commands/CommandEngine.h"
#include "commands/cmd_common.h"
#include "util/json_mini.h"
#include "viewport/Viewport.h"
#include "imgui.h"

#if defined(_WIN32)
#include <windows.h>
#include <hidusage.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dino8::input {

using kernel::Point3d;
using kernel::Vector3d;

const char* ModeName(SpaceMouseMode m) {
  switch (m) {
    case SpaceMouseMode::Camera: return "Camera";
    case SpaceMouseMode::Fly: return "Fly";
    case SpaceMouseMode::Object: return "Object";
  }
  return "Camera";
}

bool ParseMode(const std::string& s, SpaceMouseMode& out) {
  std::string t;
  for (char c : s) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (t == "camera") { out = SpaceMouseMode::Camera; return true; }
  if (t == "fly") { out = SpaceMouseMode::Fly; return true; }
  if (t == "object") { out = SpaceMouseMode::Object; return true; }
  return false;
}

std::map<int, std::string> DefaultButtonCommands() {
  // Matches the silk-screened legends on a SpaceMouse Compact/Pro: button 0
  // is always "1" (Fit in Rhino's default mapping), then the standard-view
  // keys, with the highest-numbered button opening the options ("Menu").
  return {
      {0, "ZoomExtents"},
      {1, "Top"},
      {2, "Right"},
      {3, "Front"},
      {4, "SpaceMouseOptions"},
  };
}

namespace {

double ApplyDeadZone(double v, double dz) { return std::fabs(v) < dz ? 0.0 : v; }

std::string ConfigPath() {
  return dino8::app::ConfigDirectory() + "/spacemouse.json";
}

}  // namespace

SpaceMouseDevice::SpaceMouseDevice() { options_.button_commands = DefaultButtonCommands(); }
SpaceMouseDevice::~SpaceMouseDevice() { Stop(); }

std::string SpaceMouseDevice::StatusText() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

namespace {
void SetStatusOf(SpaceMouseDevice& dev, std::mutex& m, std::string& status, const std::string& s) {
  (void)dev;
  std::lock_guard<std::mutex> lock(m);
  status = s;
}
}  // namespace

bool SpaceMouseDevice::Start(std::string& error) {
  Stop();
  if (!options_.enabled) {
    error = "SpaceMouse support is disabled (see Options > SpaceMouse)";
    SetStatusOf(*this, status_mutex_, status_, "Disabled");
    return false;
  }
  stop_requested_ = false;
  connected_ = false;
  device_name_.clear();
  SetStatusOf(*this, status_mutex_, status_, "Searching for a SpaceMouse...");
  running_ = true;
  thread_ = std::thread([this] { ThreadMain(); });
  return true;
}

void SpaceMouseDevice::Stop() {
  stop_requested_ = true;
  if (thread_.joinable()) thread_.join();
  running_ = false;
  connected_ = false;
}

void SpaceMouseDevice::PushSample(const SixDofSample& s) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(s);
  // Bound the queue: a stalled consumer (window minimized) should not
  // leak memory from a device streaming at hundreds of Hz.
  while (queue_.size() > 4096) queue_.pop_front();
}

bool SpaceMouseDevice::DrainDelta(SixDofSample& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) return false;
  out = SixDofSample{};
  for (const SixDofSample& s : queue_) {
    out.tx += s.tx; out.ty += s.ty; out.tz += s.tz;
    out.rx += s.rx; out.ry += s.ry; out.rz += s.rz;
    for (int b : s.buttons_pressed) out.buttons_pressed.push_back(b);
  }
  queue_.clear();
  return true;
}

void SpaceMouseDevice::ThreadMain() {
  std::string proto = options_.protocol;
  std::transform(proto.begin(), proto.end(), proto.begin(), [](unsigned char c) { return std::tolower(c); });
  if (proto == "file") {
    RunFile();
#if defined(_WIN32)
  } else {
    RunRawInput();
#elif defined(__APPLE__)
  } else {
    RunIOKit();
#else
  } else if (proto == "evdev") {
    RunEvdev();
  } else {
    // Auto and HidRaw both try hidraw first (the richer, vendor-defined
    // report the device actually speaks); Auto falls back to evdev if no
    // hidraw node matched, so a system that only exposes the joystick
    // node still works.
    RunHidRaw();
    if (!connected_.load() && !stop_requested_.load() && proto != "hidraw") RunEvdev();
#endif
  }
  running_ = false;
}

// ---------------------------------------------------------------------------
// Protocol=File: replays deltas from a text file for tests / demos. Lines:
//   "# comment"
//   "tx ty tz rx ry rz"            one 6-DOF sample
//   "BUTTON n"                     button n pressed on the next sample
// Paced with a short sleep between samples so a consumer polling once per
// UI frame sees them arrive over several frames, the way a real device's
// continuous stream would.
// ---------------------------------------------------------------------------
void SpaceMouseDevice::RunFile() {
  std::ifstream in(options_.file_path);
  if (!in) {
    SetStatusOf(*this, status_mutex_, status_, "File protocol: cannot open " + options_.file_path);
    return;
  }
  device_name_ = "File:" + options_.file_path;
  connected_ = true;
  SetStatusOf(*this, status_mutex_, status_, "Connected (File protocol, " + options_.file_path + ")");
  std::vector<int> pending_buttons;
  std::string line;
  while (std::getline(in, line)) {
    if (stop_requested_.load()) break;
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos || line[start] == '#') continue;
    std::istringstream ss(line.substr(start));
    std::string first;
    ss >> first;
    if (first == "BUTTON") {
      int b = 0;
      ss >> b;
      pending_buttons.push_back(b);
      continue;
    }
    SixDofSample s;
    s.tx = std::atof(first.c_str());
    ss >> s.ty >> s.tz >> s.rx >> s.ry >> s.rz;
    s.buttons_pressed = pending_buttons;
    pending_buttons.clear();
    PushSample(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  SetStatusOf(*this, status_mutex_, status_, "Connected (File protocol, " + options_.file_path + ", finished)");
}

// ---------------------------------------------------------------------------
// Linux: raw HID (/dev/hidraw*).
// ---------------------------------------------------------------------------
#if !defined(_WIN32) && !defined(__APPLE__)

namespace {
constexpr int k3DxVendorLogitech = 0x046d;   // early SpaceNavigator/SpaceMouse (badged under Logitech's VID)
constexpr int k3DxVendor3Dconnexion = 0x256f;  // current 3Dconnexion devices

bool Is3DconnexionVendor(int vendor) { return vendor == k3DxVendorLogitech || vendor == k3DxVendor3Dconnexion; }

int16_t LE16(const unsigned char* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }
}  // namespace

void SpaceMouseDevice::RunHidRaw() {
  DIR* dir = opendir("/dev");
  if (!dir) { SetStatusOf(*this, status_mutex_, status_, "No SpaceMouse found (cannot open /dev)"); return; }
  std::vector<std::string> candidates;
  while (dirent* ent = readdir(dir)) {
    std::string name = ent->d_name;
    if (name.compare(0, 7, "hidraw") == 0) candidates.push_back("/dev/" + name);
  }
  closedir(dir);
  std::sort(candidates.begin(), candidates.end());

  int fd = -1;
  std::string chosen_name = "SpaceMouse";
  for (const std::string& path : candidates) {
    int f = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (f < 0) continue;
    struct hidraw_devinfo info{};
    if (ioctl(f, HIDIOCGRAWINFO, &info) == 0 && Is3DconnexionVendor(info.vendor)) {
      char name[256] = {};
      if (ioctl(f, HIDIOCGRAWNAME(sizeof(name)), name) >= 0 && name[0]) chosen_name = name;
      fd = f;
      break;
    }
    close(f);
  }
  if (fd < 0) {
    SetStatusOf(*this, status_mutex_, status_, "No SpaceMouse found (checked " + std::to_string(candidates.size()) + " /dev/hidraw* node(s))");
    return;
  }
  device_name_ = chosen_name;
  connected_ = true;
  SetStatusOf(*this, status_mutex_, status_, "Connected: " + chosen_name + " (HID raw)");

  bool have_t = false, have_r = false;
  SixDofSample pending;
  unsigned char buf[64];
  while (!stop_requested_.load()) {
    struct pollfd pfd{fd, POLLIN, 0};
    int pr = poll(&pfd, 1, 200);
    if (pr <= 0) continue;
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 1) continue;
    const unsigned char report_id = buf[0];
    const unsigned char* payload = buf + 1;
    const size_t len = static_cast<size_t>(n) - 1;
    if (report_id == 1 && len >= 6) {
      pending.tx = LE16(payload) / 350.0;
      pending.ty = LE16(payload + 2) / 350.0;
      pending.tz = LE16(payload + 4) / 350.0;
      have_t = true;
    } else if (report_id == 2 && len >= 6) {
      pending.rx = LE16(payload) / 350.0;
      pending.ry = LE16(payload + 2) / 350.0;
      pending.rz = LE16(payload + 4) / 350.0;
      have_r = true;
    } else if (report_id == 3 && len >= 12) {
      // Combined 6-axis report (newer wireless devices).
      pending.tx = LE16(payload) / 350.0;
      pending.ty = LE16(payload + 2) / 350.0;
      pending.tz = LE16(payload + 4) / 350.0;
      pending.rx = LE16(payload + 6) / 350.0;
      pending.ry = LE16(payload + 8) / 350.0;
      pending.rz = LE16(payload + 10) / 350.0;
      have_t = have_r = true;
    } else if (report_id == 3 && len < 12) {
      // Short report id 3: button bitmask on devices that keep 1/2 split.
      unsigned bits = payload[0] | (len >= 2 ? (payload[1] << 8) : 0);
      for (int b = 0; b < 16; ++b) if (bits & (1u << b)) pending.buttons_pressed.push_back(b);
    }
    if (have_t && have_r) {
      PushSample(pending);
      pending = SixDofSample{};
      have_t = have_r = false;
    }
  }
  close(fd);
}

// ---------------------------------------------------------------------------
// Linux fallback: evdev (/dev/input/event*), for kernels/udev rules that
// only expose the puck as a generic absolute-axis joystick node.
// ---------------------------------------------------------------------------
void SpaceMouseDevice::RunEvdev() {
  DIR* dir = opendir("/dev/input");
  if (!dir) { SetStatusOf(*this, status_mutex_, status_, "No SpaceMouse found (cannot open /dev/input)"); return; }
  std::vector<std::string> candidates;
  while (dirent* ent = readdir(dir)) {
    std::string name = ent->d_name;
    if (name.compare(0, 5, "event") == 0) candidates.push_back("/dev/input/" + name);
  }
  closedir(dir);
  std::sort(candidates.begin(), candidates.end());

  int fd = -1;
  std::string chosen_name = "SpaceMouse";
  for (const std::string& path : candidates) {
    int f = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (f < 0) continue;
    struct input_id id{};
    if (ioctl(f, EVIOCGID, &id) == 0 && Is3DconnexionVendor(id.vendor)) {
      char name[256] = {};
      if (ioctl(f, EVIOCGNAME(sizeof(name)), name) >= 0 && name[0]) chosen_name = name;
      fd = f;
      break;
    }
    close(f);
  }
  if (fd < 0) {
    SetStatusOf(*this, status_mutex_, status_, "No SpaceMouse found (checked " + std::to_string(candidates.size()) + " /dev/input/event* node(s), no hidraw match either)");
    return;
  }
  device_name_ = chosen_name;
  connected_ = true;
  SetStatusOf(*this, status_mutex_, status_, "Connected: " + chosen_name + " (evdev fallback)");

  SixDofSample cur;
  while (!stop_requested_.load()) {
    struct pollfd pfd{fd, POLLIN, 0};
    if (poll(&pfd, 1, 200) <= 0) continue;
    struct input_event ev{};
    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
      if (ev.type == EV_ABS || ev.type == EV_REL) {
        // Standard 3Dconnexion evdev axis order: X,Y,Z,RX,RY,RZ.
        const double v = ev.value / 350.0;
        switch (ev.code) {
          case ABS_X: cur.tx = v; break;
          case ABS_Y: cur.ty = v; break;
          case ABS_Z: cur.tz = v; break;
          case ABS_RX: cur.rx = v; break;
          case ABS_RY: cur.ry = v; break;
          case ABS_RZ: cur.rz = v; break;
          default: break;
        }
      } else if (ev.type == EV_KEY && ev.value == 1) {
        cur.buttons_pressed.push_back(ev.code - BTN_TRIGGER >= 0 ? ev.code - BTN_TRIGGER : ev.code);
      } else if (ev.type == EV_SYN) {
        PushSample(cur);
        cur.buttons_pressed.clear();
      }
    }
  }
  close(fd);
}

#endif  // Linux

// ---------------------------------------------------------------------------
// Windows: Raw Input via a private message-only window owned by this
// thread, so no changes to the GLFW window / main.cpp WndProc are needed.
// Written carefully but untestable in this environment (no Windows CI here);
// it follows the documented RegisterRawInputDevices/WM_INPUT contract for a
// usage page 1 / usage 8 (multi-axis controller) device.
// ---------------------------------------------------------------------------
#if defined(_WIN32)

namespace {
SpaceMouseDevice* g_active_for_wndproc = nullptr;

LRESULT CALLBACK SpaceMouseWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_INPUT && g_active_for_wndproc) {
    UINT size = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size > 0 && size < 4096) {
      std::vector<BYTE> buf(size);
      if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buf.data());
        if (raw->header.dwType == RIM_TYPEHID && raw->data.hid.dwSizeHid >= 7) {
          const BYTE* report = raw->data.hid.bRawData;
          const BYTE report_id = report[0];
          const BYTE* payload = report + 1;
          auto le16 = [](const BYTE* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); };
          static SixDofSample pending;
          static bool have_t = false, have_r = false;
          if (report_id == 1) { pending.tx = le16(payload) / 350.0; pending.ty = le16(payload + 2) / 350.0; pending.tz = le16(payload + 4) / 350.0; have_t = true; }
          else if (report_id == 2) { pending.rx = le16(payload) / 350.0; pending.ry = le16(payload + 2) / 350.0; pending.rz = le16(payload + 4) / 350.0; have_r = true; }
          else if (report_id == 3 && raw->data.hid.dwSizeHid >= 13) {
            pending.tx = le16(payload) / 350.0; pending.ty = le16(payload + 2) / 350.0; pending.tz = le16(payload + 4) / 350.0;
            pending.rx = le16(payload + 6) / 350.0; pending.ry = le16(payload + 8) / 350.0; pending.rz = le16(payload + 10) / 350.0;
            have_t = have_r = true;
          } else if (report_id == 3) {
            unsigned bits = payload[0] | (payload[1] << 8);
            for (int b = 0; b < 16; ++b) if (bits & (1u << b)) pending.buttons_pressed.push_back(b);
          }
          if (have_t && have_r) {
            g_active_for_wndproc->PushSampleWin(pending);
            pending = SixDofSample{};
            have_t = have_r = false;
          }
        }
      }
    }
    return 0;
  }
  return DefWindowProc(hwnd, msg, wparam, lparam);
}
}  // namespace

void SpaceMouseDevice::PushSampleWin(const SixDofSample& s) { PushSample(s); }

void SpaceMouseDevice::RunRawInput() {
  const wchar_t* kClassName = L"Dino8SpaceMouseMsgWindow";
  WNDCLASSW wc{};
  wc.lpfnWndProc = SpaceMouseWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClassName;
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  if (!hwnd) { SetStatusOf(*this, status_mutex_, status_, "SpaceMouse: could not create the Raw Input message window"); return; }

  RAWINPUTDEVICE rid{};
  rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
  rid.usUsage = HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER;  // usage page 1, usage 8
  rid.dwFlags = RIDEV_INPUTSINK;
  rid.hwndTarget = hwnd;
  if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
    SetStatusOf(*this, status_mutex_, status_, "SpaceMouse: RegisterRawInputDevices failed");
    DestroyWindow(hwnd);
    return;
  }
  device_name_ = "SpaceMouse (Raw Input)";
  connected_ = true;
  SetStatusOf(*this, status_mutex_, status_, "Connected: Raw Input (usage page 1, usage 8)");
  g_active_for_wndproc = this;

  MSG msg;
  while (!stop_requested_.load()) {
    while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    Sleep(10);
  }
  g_active_for_wndproc = nullptr;
  DestroyWindow(hwnd);
  UnregisterClassW(kClassName, wc.hInstance);
}

#endif  // _WIN32

// ---------------------------------------------------------------------------
// macOS: IOKit HID manager, matched by vendor id, run on its own CFRunLoop.
// Also written carefully but untestable here (no macOS CI in this
// environment).
// ---------------------------------------------------------------------------
#if defined(__APPLE__)

namespace {
void HidInputCallback(void* context, IOReturn, void*, IOHIDValueRef value) {
  auto* dev = static_cast<SpaceMouseDevice*>(context);
  IOHIDElementRef element = IOHIDValueGetElement(value);
  const uint32_t usage_page = IOHIDElementGetUsagePage(element);
  const uint32_t usage = IOHIDElementGetUsage(element);
  const CFIndex v = IOHIDValueGetIntegerValue(value);
  if (usage_page == kHIDPage_GenericDesktop) {
    static SixDofSample pending;
    const double scaled = static_cast<double>(v) / 350.0;
    bool got_axis = true;
    switch (usage) {
      case kHIDUsage_GD_X: pending.tx = scaled; break;
      case kHIDUsage_GD_Y: pending.ty = scaled; break;
      case kHIDUsage_GD_Z: pending.tz = scaled; break;
      case kHIDUsage_GD_Rx: pending.rx = scaled; break;
      case kHIDUsage_GD_Ry: pending.ry = scaled; break;
      case kHIDUsage_GD_Rz: pending.rz = scaled; got_axis = true; dev->PushSampleMac(pending); break;
      default: got_axis = false; break;
    }
    (void)got_axis;
  } else if (usage_page == kHIDPage_Button && v == 1) {
    SixDofSample btn;
    btn.buttons_pressed.push_back(static_cast<int>(usage) - 1);
    dev->PushSampleMac(btn);
  }
}

void HidDeviceMatched(void* context, IOReturn, void*, IOHIDDeviceRef device) {
  auto* dev = static_cast<SpaceMouseDevice*>(context);
  CFStringRef product = static_cast<CFStringRef>(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey)));
  char name[256] = "SpaceMouse";
  if (product) CFStringGetCString(product, name, sizeof(name), kCFStringEncodingUTF8);
  dev->SetConnectedMac(name);
}
}  // namespace

void SpaceMouseDevice::PushSampleMac(const SixDofSample& s) { PushSample(s); }
void SpaceMouseDevice::SetConnectedMac(const std::string& name) {
  device_name_ = name;
  connected_ = true;
  SetStatusOf(*this, status_mutex_, status_, "Connected: " + name + " (IOKit HID)");
}

void SpaceMouseDevice::RunIOKit() {
  IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  const int vendors[] = {0x046d, 0x256f};
  CFMutableArrayRef matches = CFArrayCreateMutable(kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks);
  for (int vendor : vendors) {
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNumberRef v = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), v);
    CFRelease(v);
    CFArrayAppendValue(matches, dict);
    CFRelease(dict);
  }
  IOHIDManagerSetDeviceMatchingMultiple(manager, matches);
  CFRelease(matches);
  IOHIDManagerRegisterDeviceMatchingCallback(manager, HidDeviceMatched, this);
  IOHIDManagerRegisterInputValueCallback(manager, HidInputCallback, this);
  IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
  SetStatusOf(*this, status_mutex_, status_, "Searching for a SpaceMouse (IOKit)...");
  while (!stop_requested_.load()) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, true);
  }
  IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
  CFRelease(manager);
}

#endif  // __APPLE__

// ---------------------------------------------------------------------------
// Persistence: <config>/spacemouse.json.
// ---------------------------------------------------------------------------
bool SpaceMouseDevice::LoadPersisted() {
  std::ifstream in(ConfigPath());
  if (!in) return false;
  std::stringstream buf;
  buf << in.rdbuf();
  json::Value root;
  std::string err;
  if (!json::Parse(buf.str(), root, err) || !root.IsObject()) return false;
  auto num = [](const json::Value& v, double fb) { return v.type == json::Value::Type::Number ? v.number : fb; };
  auto boolean = [](const json::Value& v, bool fb) { return v.type == json::Value::Type::Bool ? v.boolean : fb; };
  options_.enabled = boolean(root["enabled"], options_.enabled);
  if (root["protocol"].IsString()) options_.protocol = root["protocol"].AsString();
  if (root["file_path"].IsString()) options_.file_path = root["file_path"].AsString();
  options_.translation_sensitivity = num(root["translation_sensitivity"], options_.translation_sensitivity);
  options_.rotation_sensitivity = num(root["rotation_sensitivity"], options_.rotation_sensitivity);
  options_.invert_tx = boolean(root["invert_tx"], options_.invert_tx);
  options_.invert_ty = boolean(root["invert_ty"], options_.invert_ty);
  options_.invert_tz = boolean(root["invert_tz"], options_.invert_tz);
  options_.invert_rx = boolean(root["invert_rx"], options_.invert_rx);
  options_.invert_ry = boolean(root["invert_ry"], options_.invert_ry);
  options_.invert_rz = boolean(root["invert_rz"], options_.invert_rz);
  options_.dead_zone = num(root["dead_zone"], options_.dead_zone);
  options_.dominant_axis = boolean(root["dominant_axis"], options_.dominant_axis);
  if (root["mode"].IsString()) ParseMode(root["mode"].AsString(), options_.mode);
  const json::Value& buttons = root["button_commands"];
  if (buttons.IsObject()) {
    for (const auto& [k, v] : buttons.object) if (v.IsString()) options_.button_commands[std::atoi(k.c_str())] = v.AsString();
  }
  return true;
}

bool SpaceMouseDevice::SavePersisted() const {
  std::ofstream out(ConfigPath());
  if (!out) return false;
  auto b = [](bool v) { return v ? "true" : "false"; };
  out << "{\n";
  out << "  \"enabled\": " << b(options_.enabled) << ",\n";
  out << "  \"protocol\": \"" << options_.protocol << "\",\n";
  out << "  \"file_path\": \"" << options_.file_path << "\",\n";
  out << "  \"translation_sensitivity\": " << options_.translation_sensitivity << ",\n";
  out << "  \"rotation_sensitivity\": " << options_.rotation_sensitivity << ",\n";
  out << "  \"invert_tx\": " << b(options_.invert_tx) << ", \"invert_ty\": " << b(options_.invert_ty) << ", \"invert_tz\": " << b(options_.invert_tz) << ",\n";
  out << "  \"invert_rx\": " << b(options_.invert_rx) << ", \"invert_ry\": " << b(options_.invert_ry) << ", \"invert_rz\": " << b(options_.invert_rz) << ",\n";
  out << "  \"dead_zone\": " << options_.dead_zone << ",\n";
  out << "  \"dominant_axis\": " << b(options_.dominant_axis) << ",\n";
  out << "  \"mode\": \"" << ModeName(options_.mode) << "\",\n";
  out << "  \"button_commands\": {";
  bool first = true;
  for (const auto& [k, v] : options_.button_commands) { out << (first ? "" : ", ") << "\"" << k << "\": \"" << v << "\""; first = false; }
  out << "}\n";
  out << "}\n";
  return true;
}

// ---------------------------------------------------------------------------
// Process-wide device + app integration.
// ---------------------------------------------------------------------------
SpaceMouseDevice& Device() {
  static SpaceMouseDevice device;
  return device;
}

namespace {
bool g_panel_visible = false;

// Applies one drained (already summed) sample to the active viewport's
// camera, or to the current selection in Object mode. Uses only the public
// Viewport/Camera/Document API, so this file is the entire integration
// surface - Viewport.h/.cpp need no changes for SpaceMouse support.
void ApplySample(dino8::app::Application& app, const SixDofSample& raw) {
  const SpaceMouseOptions& o = Device().Options();
  double tx = ApplyDeadZone(raw.tx, o.dead_zone) * (o.invert_tx ? -1 : 1) * o.translation_sensitivity;
  double ty = ApplyDeadZone(raw.ty, o.dead_zone) * (o.invert_ty ? -1 : 1) * o.translation_sensitivity;
  double tz = ApplyDeadZone(raw.tz, o.dead_zone) * (o.invert_tz ? -1 : 1) * o.translation_sensitivity;
  double rx = ApplyDeadZone(raw.rx, o.dead_zone) * (o.invert_rx ? -1 : 1) * o.rotation_sensitivity;
  double ry = ApplyDeadZone(raw.ry, o.dead_zone) * (o.invert_ry ? -1 : 1) * o.rotation_sensitivity;
  double rz = ApplyDeadZone(raw.rz, o.dead_zone) * (o.invert_rz ? -1 : 1) * o.rotation_sensitivity;

  if (o.dominant_axis) {
    double axes[6] = {tx, ty, tz, rx, ry, rz};
    int dominant = 0;
    for (int i = 1; i < 6; ++i) if (std::fabs(axes[i]) > std::fabs(axes[dominant])) dominant = i;
    for (int i = 0; i < 6; ++i) if (i != dominant) axes[i] = 0;
    tx = axes[0]; ty = axes[1]; tz = axes[2]; rx = axes[3]; ry = axes[4]; rz = axes[5];
  }
  // Buttons fire independently of motion: a real SpaceMouse is at rest
  // (every axis exactly zero) far more often than not when its button is
  // pressed, so gating this on nonzero motion would drop nearly every
  // button press. Run them first, then bail out of the motion handling
  // below (but not the whole function) if nothing moved.
  const bool has_motion = tx != 0 || ty != 0 || tz != 0 || rx != 0 || ry != 0 || rz != 0;
  auto run_buttons = [&] {
    for (int b : raw.buttons_pressed) {
      auto it = o.button_commands.find(b);
      if (it != o.button_commands.end()) app.Engine().Execute(it->second);
    }
  };

  if (o.mode == SpaceMouseMode::Object) {
    std::vector<dino8::app::ObjectId> sel = app.Doc().SelectedIds();
    if (has_motion && !sel.empty()) {
      dino8::app::Viewport* vp = app.ActiveViewport();
      const dino8::app::ConstructionPlane& cp = vp ? vp->CPlane() : dino8::app::ConstructionPlane{};
      Vector3d move = cp.x_axis * (tx * 2.0) + cp.y_axis * (-ty * 2.0) + cp.Normal() * (tz * 2.0);
      ON_Xform xf = ON_Xform::TranslationTransformation(move);
      if (std::fabs(rz) > 1e-9) {
        kernel::BoundingBox bb;
        if (app.Doc().BoundingBoxOf(sel, bb)) {
          Point3d center = (bb.min + bb.max) / 2.0;
          ON_Xform rot;
          rot.Rotation(rz * 0.05, cp.Normal(), center);
          xf = rot * xf;
        }
      }
      app.Doc().BeginChange("SpaceMouse");
      for (dino8::app::ObjectId id : sel) {
        if (dino8::app::SceneObject* so = app.Doc().Find(id)) so->Transform(xf);
      }
    }
    run_buttons();
    return;
  }

  dino8::app::Viewport* vp = app.ActiveViewport();
  if (has_motion && vp) {
    dino8::app::Camera& cam = vp->GetCamera();
    const double pan_scale = 6.0;
    if (o.mode == SpaceMouseMode::Fly) {
      dino8::app::CameraState st = cam.State();
      Vector3d fwd = cam.Forward(), right = cam.Right(), up = cam.Up();
      const double dist = std::max(1.0, cam.Distance());
      Vector3d move = right * (tx * dist * 0.05) + up * (-ty * dist * 0.05) + fwd * (tz * dist * 0.05);
      st.eye = st.eye + move;
      st.target = st.target + move;
      // Yaw/pitch: rotate the eye->target vector about world Z (yaw, ry) and
      // the camera's own right axis (pitch, rx).
      Vector3d dir = st.target - st.eye;
      ON_Xform yaw; yaw.Rotation(-ry * 0.05, Vector3d(0, 0, 1), ON_3dPoint::Origin);
      dir = yaw * dir;
      ON_Xform pitch; pitch.Rotation(-rx * 0.05, right, ON_3dPoint::Origin);
      dir = pitch * dir;
      st.target = st.eye + dir;
      cam.SetState(st);
    } else {
      if (tx != 0 || ty != 0) cam.Pan(-tx * pan_scale, ty * pan_scale, vp->Width(), vp->Height());
      if (tz != 0) cam.Dolly(-tz * 8.0);
      if (rx != 0 || ry != 0) cam.Orbit(ry * 120.0, rx * 120.0);
      if (rz != 0) cam.RotateAboutViewAxis(rz * 6.0);
    }
  }
  run_buttons();
}
}  // namespace

void Init(dino8::app::Application& app) {
  Device().LoadPersisted();
  std::string error;
  if (Device().Options().enabled) Device().Start(error);
  (void)app;
}

void Shutdown() { Device().SavePersisted(); Device().Stop(); }

void Frame(dino8::app::Application& app) {
  SixDofSample s;
  while (Device().DrainDelta(s)) ApplySample(app, s);
  DrawOptionsPanel(app);
}

void ShowOptionsPanel() { g_panel_visible = true; }
bool OptionsPanelVisible() { return g_panel_visible; }

void DrawOptionsPanel(dino8::app::Application& app) {
  if (!g_panel_visible) return;
  ImGui::SetNextWindowSize(ImVec2(420, 420), ImGuiCond_Appearing);
  if (!ImGui::Begin("SpaceMouse Options", &g_panel_visible)) { ImGui::End(); return; }
  SpaceMouseDevice& dev = Device();
  SpaceMouseOptions& o = dev.Options();
  ImGui::TextWrapped("%s", dev.StatusText().c_str());
  ImGui::Separator();
  if (ImGui::Checkbox("Enabled", &o.enabled)) { std::string err; if (o.enabled) dev.Start(err); else dev.Stop(); }
  static const char* protocols[] = {"Auto", "HidRaw", "Evdev", "RawInput", "IOKit", "File"};
  int proto_index = 0;
  for (int i = 0; i < 6; ++i) if (o.protocol == protocols[i]) proto_index = i;
  if (ImGui::Combo("Protocol", &proto_index, protocols, 6)) { o.protocol = protocols[proto_index]; std::string err; dev.Start(err); }
  if (o.protocol == "File") {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s", o.file_path.c_str());
    if (ImGui::InputText("Deltas file", buf, sizeof(buf))) o.file_path = buf;
    if (ImGui::Button("Reload")) { std::string err; dev.Start(err); }
  }
  int mode_index = static_cast<int>(o.mode);
  static const char* modes[] = {"Camera", "Fly", "Object"};
  if (ImGui::Combo("Mode", &mode_index, modes, 3)) o.mode = static_cast<SpaceMouseMode>(mode_index);
  float trans_sens = static_cast<float>(o.translation_sensitivity);
  if (ImGui::SliderFloat("Translation sensitivity", &trans_sens, 0.1f, 4.0f)) o.translation_sensitivity = trans_sens;
  float rot_sens = static_cast<float>(o.rotation_sensitivity);
  if (ImGui::SliderFloat("Rotation sensitivity", &rot_sens, 0.1f, 4.0f)) o.rotation_sensitivity = rot_sens;
  float dead_zone = static_cast<float>(o.dead_zone);
  if (ImGui::SliderFloat("Dead zone", &dead_zone, 0.0f, 0.3f)) o.dead_zone = dead_zone;
  ImGui::Checkbox("Dominant axis only", &o.dominant_axis);
  ImGui::Text("Invert:");
  ImGui::SameLine(); ImGui::Checkbox("X", &o.invert_tx); ImGui::SameLine(); ImGui::Checkbox("Y", &o.invert_ty); ImGui::SameLine(); ImGui::Checkbox("Z", &o.invert_tz);
  ImGui::SameLine(); ImGui::Checkbox("RX", &o.invert_rx); ImGui::SameLine(); ImGui::Checkbox("RY", &o.invert_ry); ImGui::SameLine(); ImGui::Checkbox("RZ", &o.invert_rz);
  ImGui::Separator();
  ImGui::TextWrapped("Buttons: 0=Fit 1=Top 2=Right 3=Front, highest button=Menu (this page). Edit in spacemouse.json.");
  if (ImGui::Button("Save")) dev.SavePersisted();
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Commands: SpaceMouse (status + opens the options page) and its
// 3DconnexionOptions alias (Rhino has no such command; both are extras
// beyond the 1055-command catalogue, as the audit calls for).
// ---------------------------------------------------------------------------
void RegisterSpaceMouseCommands(dino8::app::CommandEngine& e) {
  using namespace dino8::app;
  Reg(e, "SpaceMouse", Immediate([](CommandContext& ctx) {
        SpaceMouseDevice& dev = Device();
        ctx.Print(std::string("SpaceMouse: ") + (dev.Connected() ? "connected (" + dev.DeviceName() + ")" : "not connected") +
                   " - " + dev.StatusText() + " - mode " + ModeName(dev.Options().mode) + ", protocol " + dev.Options().protocol);
        ShowOptionsPanel();
        ctx.App().Panels().options = true;
      }));
  Reg(e, "SpaceMouseOptions", Immediate([](CommandContext& ctx) { ShowOptionsPanel(); ctx.App().Panels().options = true; }));
  Reg(e, "3DconnexionOptions", Immediate([](CommandContext& ctx) { ShowOptionsPanel(); ctx.App().Panels().options = true; }), CommandStatus::Implemented, "Alias for SpaceMouseOptions.");
  Reg(e, "SpaceMouseProtocol", Immediate([](CommandContext& ctx) {
        if (auto t = ctx.Engine().TakePendingInput()) {
          Device().Options().protocol = *t;
          std::string lower = *t;
          std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
          if (lower == "file") { if (auto path = ctx.Engine().TakePendingInput()) Device().Options().file_path = *path; }
          std::string err;
          Device().Start(err);
          ctx.Print("SpaceMouse protocol set to " + *t + (err.empty() ? "" : (": " + err)));
        } else {
          ctx.Print("SpaceMouse protocol: " + Device().Options().protocol);
        }
      }), CommandStatus::Implemented, "SpaceMouseProtocol <Auto|HidRaw|Evdev|RawInput|IOKit|File> [path]. With File, a second token is the deltas file path.");
  Reg(e, "SpaceMouseMode", Immediate([](CommandContext& ctx) {
        if (auto t = ctx.Engine().TakePendingInput()) {
          SpaceMouseMode m;
          if (ParseMode(*t, m)) { Device().Options().mode = m; ctx.Print("SpaceMouse mode set to " + std::string(ModeName(m))); }
          else ctx.Warn("Unknown SpaceMouse mode '" + *t + "' (want Camera, Fly or Object)");
        } else {
          ctx.Print("SpaceMouse mode: " + std::string(ModeName(Device().Options().mode)));
        }
      }), CommandStatus::Implemented, "SpaceMouseMode <Camera|Fly|Object> - scriptable equivalent of the Options > SpaceMouse Mode combo.");
}

}  // namespace dino8::input
