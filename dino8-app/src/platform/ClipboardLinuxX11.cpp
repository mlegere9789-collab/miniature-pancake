// Linux OS-clipboard image support: a real, in-process ICCCM selection
// owner over Xlib - no shelling out to xclip/xsel, no writing a file and
// calling it done.
//
// How ICCCM clipboard ownership actually works, and why this needs a
// background thread: X11 has no "clipboard service" - the clipboard *is*
// whichever client currently owns the CLIPBOARD selection. Owning it means
// answering, for as long as you hold it, every SelectionRequest event any
// other client sends (e.g. when the user presses Ctrl+V in some other
// app): you write the requested format's bytes into a property on *their*
// window and send back a SelectionNotify. That has to keep happening for
// the lifetime of the ownership, which is exactly why the app's own
// GLFW/ImGui frame loop can't do it inline - a command handler that returns
// immediately after XSetSelectionOwner() would leave nothing running to
// service the next paste. So this opens its own X11 connection (distinct
// from GLFW's), separate from the app's window/event loop entirely, and a
// dedicated thread blocks in XNextEvent() on it for as long as the process
// runs, answering TARGETS/image-png/image-bmp requests from whatever's
// currently stored. This is the real mechanism - not a stand-in for one.
//
// XInitThreads() is called once before the first XOpenDisplay() so Xlib
// itself serializes access to that connection; WriteImageToClipboard (the
// app/command thread) can then safely call XSetSelectionOwner directly
// while the background thread is concurrently blocked in XNextEvent on the
// same Display*.
#if defined(__linux__)

#include "platform/ClipboardPlatform.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

namespace dino8::platform {

namespace {

class X11ClipboardServer {
 public:
  bool WriteData(std::vector<unsigned char> png, std::vector<unsigned char> bmp, std::string& error) {
    if (!EnsureStarted(error)) return false;
    {
      std::lock_guard<std::mutex> lock(data_mutex_);
      png_ = std::move(png);
      bmp_ = std::move(bmp);
      have_data_ = true;
    }
    XSetSelectionOwner(display_, clipboard_atom_, window_, CurrentTime);
    XFlush(display_);
    if (XGetSelectionOwner(display_, clipboard_atom_) != window_) {
      error = "X server refused CLIPBOARD ownership (another client raced us for it)";
      return false;
    }
    return true;
  }

  void Shutdown() {
    if (!running_.exchange(false)) return;
    if (display_) {
      // Wake the blocked XNextEvent() with a message addressed to our own
      // window so Loop() re-checks `running_` and exits promptly.
      XClientMessageEvent wake{};
      wake.type = ClientMessage;
      wake.window = window_;
      wake.message_type = wake_atom_;
      wake.format = 32;
      XSendEvent(display_, window_, False, NoEventMask, reinterpret_cast<XEvent*>(&wake));
      XFlush(display_);
    }
    if (thread_.joinable()) thread_.join();
    if (display_) {
      XSetSelectionOwner(display_, clipboard_atom_, None, CurrentTime);
      XCloseDisplay(display_);
      display_ = nullptr;
    }
  }

  ~X11ClipboardServer() { Shutdown(); }

 private:
  bool EnsureStarted(std::string& error) {
    static std::once_flag xinit_flag;
    std::call_once(xinit_flag, [] { XInitThreads(); });
    std::lock_guard<std::mutex> lock(start_mutex_);
    if (running_.load()) return true;
    display_ = XOpenDisplay(nullptr);
    if (!display_) {
      error = "Cannot open the X11 display (is DISPLAY set to a running X server / Xvfb?)";
      return false;
    }
    const int screen = DefaultScreen(display_);
    window_ = XCreateSimpleWindow(display_, RootWindow(display_, screen), 0, 0, 1, 1, 0, 0, 0);
    clipboard_atom_ = XInternAtom(display_, "CLIPBOARD", False);
    targets_atom_ = XInternAtom(display_, "TARGETS", False);
    png_atom_ = XInternAtom(display_, "image/png", False);
    bmp_atom_ = XInternAtom(display_, "image/bmp", False);
    wake_atom_ = XInternAtom(display_, "DINO8_CLIPBOARD_WAKE", False);
    running_.store(true);
    thread_ = std::thread([this] { Loop(); });
    return true;
  }

  void Loop() {
    while (running_.load()) {
      XEvent ev;
      XNextEvent(display_, &ev);  // blocks until an event arrives on this connection
      if (!running_.load()) break;
      if (ev.type == ClientMessage && ev.xclient.window == window_ && ev.xclient.message_type == wake_atom_) {
        continue;  // just Shutdown()'s wake-up; loop condition is re-checked above
      }
      if (ev.type == SelectionRequest) {
        HandleSelectionRequest(ev.xselectionrequest);
      }
      // SelectionClear (we lost ownership to another app) needs no action:
      // we simply stop being asked until WriteData() reclaims ownership.
    }
  }

  void HandleSelectionRequest(const XSelectionRequestEvent& req) {
    XSelectionEvent notify{};
    notify.type = SelectionNotify;
    notify.display = req.display;
    notify.requestor = req.requestor;
    notify.selection = req.selection;
    notify.target = req.target;
    notify.time = req.time;
    notify.property = None;  // refuse by default

    const Atom prop = (req.property != None) ? req.property : req.target;
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (req.target == targets_atom_) {
      Atom targets[3] = {targets_atom_, png_atom_, bmp_atom_};
      XChangeProperty(display_, req.requestor, prop, XA_ATOM, 32, PropModeReplace,
                       reinterpret_cast<unsigned char*>(targets), 3);
      notify.property = prop;
    } else if (req.target == png_atom_ && have_data_ && !png_.empty()) {
      XChangeProperty(display_, req.requestor, prop, png_atom_, 8, PropModeReplace, png_.data(),
                       static_cast<int>(png_.size()));
      notify.property = prop;
    } else if (req.target == bmp_atom_ && have_data_ && !bmp_.empty()) {
      XChangeProperty(display_, req.requestor, prop, bmp_atom_, 8, PropModeReplace, bmp_.data(),
                       static_cast<int>(bmp_.size()));
      notify.property = prop;
    }
    XSendEvent(display_, req.requestor, False, NoEventMask, reinterpret_cast<XEvent*>(&notify));
    XFlush(display_);
  }

  Display* display_ = nullptr;
  Window window_ = 0;
  Atom clipboard_atom_ = 0, targets_atom_ = 0, png_atom_ = 0, bmp_atom_ = 0, wake_atom_ = 0;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex start_mutex_;
  std::mutex data_mutex_;
  std::vector<unsigned char> png_, bmp_;
  bool have_data_ = false;
};

X11ClipboardServer& Server() {
  static X11ClipboardServer server;
  return server;
}

}  // namespace

bool PlatformWriteImageToClipboard(const std::vector<unsigned char>& png, const std::vector<unsigned char>& bmp,
                                    int /*width*/, int /*height*/, std::string& error) {
  return Server().WriteData(png, bmp, error);
}

void PlatformShutdownClipboard() { Server().Shutdown(); }

}  // namespace dino8::platform

#endif  // __linux__
