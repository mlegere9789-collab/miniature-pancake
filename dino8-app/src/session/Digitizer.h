// A 3D digitizer connection (Rhino's Dig* commands): a serial-port ASCII
// point stream, or one of two headless stand-ins that make the feature
// testable without hardware - Protocol=File replays "x y z [button]"
// lines from a text file, and Protocol=Simulated turns the next mouse
// pick into a digitized point.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dino8/kernel/types.h"

namespace dino8::app {

enum class DigitizerProtocol { Ascii, File, Simulated };
const char* DigitizerProtocolName(DigitizerProtocol p);
bool ParseDigitizerProtocol(const std::string& text, DigitizerProtocol& out);

// One point read from the device/file/mouse, before calibration.
struct DigitizerPoint {
  kernel::Point3d raw{0, 0, 0};
  int button = 0;
  bool valid = false;
};

// A rigid transform from digitizer space into model space, solved from
// three calibration points (origin, a point on +X, a point on +Y) via
// DigCalibrate: origin maps to (0,0,0) (or the active CPlane origin),
// the origin->X direction becomes model +X, and the plane normal of the
// three points becomes model +Z.
struct DigitizerCalibration {
  bool set = false;
  kernel::Point3d origin{0, 0, 0};
  kernel::Vector3d x_axis{1, 0, 0};
  kernel::Vector3d y_axis{0, 1, 0};
  kernel::Point3d target_origin{0, 0, 0};
  kernel::Vector3d target_x{1, 0, 0};
  kernel::Vector3d target_y{0, 1, 0};
};

// One process-wide digitizer connection - real hardware has one stylus,
// so a singleton mirrors that and lets every Dig* command share the same
// connection without threading it through CommandContext.
class Digitizer {
 public:
  static Digitizer& Instance();

  // Protocol=Ascii: opens a serial port (POSIX termios on Linux/macOS,
  // Win32 CreateFile/SetCommState on Windows) at `baud`, non-blocking.
  bool ConnectSerial(const std::string& port, int baud, std::string& error);
  // Protocol=File: reads whitespace/comma separated "x y z [button]"
  // points from a text file up front; ReadPoint() then hands them out one
  // per call, in order - a script or test can drive it deterministically.
  bool ConnectFile(const std::string& path, std::string& error);
  // Protocol=Simulated: no device; FeedSimulatedPoint() (called from the
  // viewport click handler while a Dig* command is connected in this
  // mode) supplies the next point.
  void ConnectSimulated();
  void Disconnect();
  bool Connected() const { return connected_; }

  DigitizerProtocol Protocol() const { return protocol_; }
  const std::string& Port() const { return port_; }
  int Baud() const { return baud_; }
  bool Paused() const { return paused_; }
  void SetPaused(bool p) { paused_ = p; }

  // Non-blocking: returns false when no new point is ready yet.
  bool ReadPoint(DigitizerPoint& out);
  void FeedSimulatedPoint(kernel::Point3d p, int button = 0);

  DigitizerCalibration& Calibration() { return calibration_; }
  // Applies the calibration (identity when none is set) and the unit
  // scale to a raw digitized point.
  kernel::Point3d ToModel(kernel::Point3d raw) const;

  double UnitScale() const { return unit_scale_; }
  void SetUnitScale(double s) { unit_scale_ = s > 0 ? s : 1.0; }

  std::optional<DigitizerPoint> LastRawPoint() const { return last_point_; }

  // /dev/tty* (or COM1..COM32 on Windows) that currently exist / open
  // successfully - the Digitizer panel's port list.
  static std::vector<std::string> EnumeratePorts();

 private:
  Digitizer() = default;
  ~Digitizer();
  Digitizer(const Digitizer&) = delete;
  Digitizer& operator=(const Digitizer&) = delete;

  bool ParseLine(const std::string& line, DigitizerPoint& out) const;
  void ResetConnection();

  DigitizerProtocol protocol_ = DigitizerProtocol::File;
  bool connected_ = false;
  std::string port_;
  int baud_ = 9600;
  bool paused_ = false;

  // Serial (Ascii): a POSIX fd, or -1. Windows keeps its HANDLE separately
  // (opaque void* so this header stays Win32-free on other platforms).
  int fd_ = -1;
  void* win_handle_ = nullptr;
  std::string read_buffer_;

  // File.
  std::vector<std::string> file_lines_;
  size_t file_cursor_ = 0;

  // Simulated.
  std::optional<DigitizerPoint> simulated_pending_;

  DigitizerCalibration calibration_;
  double unit_scale_ = 1.0;
  std::optional<DigitizerPoint> last_point_;
};

}  // namespace dino8::app
