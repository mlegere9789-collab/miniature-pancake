#include "session/Digitizer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <opennurbs.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace dino8::app {

const char* DigitizerProtocolName(DigitizerProtocol p) {
  switch (p) {
    case DigitizerProtocol::Ascii: return "Ascii";
    case DigitizerProtocol::File: return "File";
    case DigitizerProtocol::Simulated: return "Simulated";
  }
  return "Ascii";
}

bool ParseDigitizerProtocol(const std::string& text, DigitizerProtocol& out) {
  std::string t = text;
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (t == "ascii") { out = DigitizerProtocol::Ascii; return true; }
  if (t == "file") { out = DigitizerProtocol::File; return true; }
  if (t == "simulated" || t == "sim") { out = DigitizerProtocol::Simulated; return true; }
  return false;
}

Digitizer& Digitizer::Instance() {
  static Digitizer instance;
  return instance;
}

Digitizer::~Digitizer() { Disconnect(); }

void Digitizer::ResetConnection() {
#if defined(_WIN32)
  if (win_handle_ && win_handle_ != INVALID_HANDLE_VALUE) CloseHandle(static_cast<HANDLE>(win_handle_));
  win_handle_ = nullptr;
#else
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
#endif
  read_buffer_.clear();
  file_lines_.clear();
  file_cursor_ = 0;
  simulated_pending_.reset();
}

void Digitizer::Disconnect() {
  ResetConnection();
  connected_ = false;
  port_.clear();
}

// Tolerant line parser: comma or space separated, an optional leading
// non-numeric tag (e.g. a MicroScribe/Faro-style "PT" prefix or a frame
// index) is skipped, and a trailing integer beyond x,y,z is the button
// state. Anything that doesn't yield 3 numbers is rejected.
bool Digitizer::ParseLine(const std::string& line, DigitizerPoint& out) const {
  std::string s = line;
  for (char& c : s) if (c == ',' ) c = ' ';
  std::istringstream ss(s);
  std::vector<double> nums;
  std::string tok;
  while (ss >> tok) {
    char* end = nullptr;
    double v = std::strtod(tok.c_str(), &end);
    if (end && *end == 0 && !tok.empty()) nums.push_back(v);
    // Non-numeric tokens (a device tag, a checksum letter...) are skipped
    // rather than aborting the whole line.
  }
  if (nums.size() < 3) return false;
  out.raw = kernel::Point3d(nums[0], nums[1], nums[2]);
  out.button = nums.size() >= 4 ? static_cast<int>(nums[3]) : 0;
  out.valid = true;
  return true;
}

bool Digitizer::ConnectFile(const std::string& path, std::string& error) {
  std::ifstream in(path);
  if (!in) { error = "Could not open " + path; return false; }
  ResetConnection();
  std::string line;
  while (std::getline(in, line)) {
    // Skip blank lines and '#' comments so a hand-written test fixture
    // can be readable.
    size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || line[first] == '#') continue;
    file_lines_.push_back(line);
  }
  protocol_ = DigitizerProtocol::File;
  port_ = path;
  connected_ = true;
  paused_ = false;
  return true;
}

void Digitizer::ConnectSimulated() {
  ResetConnection();
  protocol_ = DigitizerProtocol::Simulated;
  port_ = "Simulated";
  connected_ = true;
  paused_ = false;
}

#if defined(_WIN32)
bool Digitizer::ConnectSerial(const std::string& port, int baud, std::string& error) {
  ResetConnection();
  // Windows device names need the \\.\ prefix for COM10 and above.
  const std::string device = port.rfind("\\\\.\\", 0) == 0 ? port : ("\\\\.\\" + port);
  HANDLE h = CreateFileA(device.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) { error = "Could not open " + port; return false; }
  DCB dcb{};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(h, &dcb)) { CloseHandle(h); error = "GetCommState failed on " + port; return false; }
  dcb.BaudRate = static_cast<DWORD>(baud);
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  SetCommState(h, &dcb);
  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  SetCommTimeouts(h, &timeouts);
  win_handle_ = h;
  protocol_ = DigitizerProtocol::Ascii;
  port_ = port;
  baud_ = baud;
  connected_ = true;
  paused_ = false;
  return true;
}

namespace {
bool TryOpenSerial(const std::string& name) {
  const std::string device = "\\\\.\\" + name;
  HANDLE h = CreateFileA(device.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  CloseHandle(h);
  return true;
}
}  // namespace

std::vector<std::string> Digitizer::EnumeratePorts() {
  std::vector<std::string> out;
  for (int i = 1; i <= 32; ++i) {
    const std::string name = "COM" + std::to_string(i);
    if (TryOpenSerial(name)) out.push_back(name);
  }
  return out;
}

bool Digitizer::ReadPoint(DigitizerPoint& out) {
  if (!connected_ || paused_) return false;
  if (protocol_ == DigitizerProtocol::File) {
    if (file_cursor_ >= file_lines_.size()) return false;
    while (file_cursor_ < file_lines_.size()) {
      const std::string line = file_lines_[file_cursor_++];
      if (ParseLine(line, out)) { last_point_ = out; return true; }
    }
    return false;
  }
  if (protocol_ == DigitizerProtocol::Simulated) {
    if (!simulated_pending_) return false;
    out = *simulated_pending_;
    simulated_pending_.reset();
    last_point_ = out;
    return true;
  }
  if (!win_handle_) return false;
  char buf[256];
  DWORD read = 0;
  if (!ReadFile(static_cast<HANDLE>(win_handle_), buf, sizeof(buf), &read, nullptr) || read == 0) return false;
  read_buffer_.append(buf, read);
  size_t nl;
  while ((nl = read_buffer_.find('\n')) != std::string::npos) {
    const std::string line = read_buffer_.substr(0, nl);
    read_buffer_.erase(0, nl + 1);
    if (ParseLine(line, out)) { last_point_ = out; return true; }
  }
  return false;
}
#else
bool Digitizer::ConnectSerial(const std::string& port, int baud, std::string& error) {
  ResetConnection();
  const int fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) { error = "Could not open " + port; return false; }
  termios tty{};
  if (tcgetattr(fd, &tty) != 0) { close(fd); error = "tcgetattr failed on " + port; return false; }
  speed_t speed = B9600;
  switch (baud) {
    case 1200: speed = B1200; break;
    case 2400: speed = B2400; break;
    case 4800: speed = B4800; break;
    case 9600: speed = B9600; break;
    case 19200: speed = B19200; break;
    case 38400: speed = B38400; break;
    case 57600: speed = B57600; break;
    case 115200: speed = B115200; break;
    default: speed = B9600; break;
  }
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | CSTOPB);
  tty.c_lflag = 0;  // raw
  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_oflag = 0;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;
  if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); error = "tcsetattr failed on " + port; return false; }
  fd_ = fd;
  protocol_ = DigitizerProtocol::Ascii;
  port_ = port;
  baud_ = baud;
  connected_ = true;
  paused_ = false;
  return true;
}

std::vector<std::string> Digitizer::EnumeratePorts() {
  std::vector<std::string> out;
  DIR* dir = opendir("/dev");
  if (!dir) return out;
  while (dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0 || name.rfind("ttyS", 0) == 0 || name.rfind("cu.", 0) == 0) {
      const std::string path = "/dev/" + name;
      const int fd = open(path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
      if (fd >= 0) { close(fd); out.push_back(path); }
    }
  }
  closedir(dir);
  std::sort(out.begin(), out.end());
  return out;
}

bool Digitizer::ReadPoint(DigitizerPoint& out) {
  if (!connected_ || paused_) return false;
  if (protocol_ == DigitizerProtocol::File) {
    while (file_cursor_ < file_lines_.size()) {
      const std::string line = file_lines_[file_cursor_++];
      if (ParseLine(line, out)) { last_point_ = out; return true; }
    }
    return false;
  }
  if (protocol_ == DigitizerProtocol::Simulated) {
    if (!simulated_pending_) return false;
    out = *simulated_pending_;
    simulated_pending_.reset();
    last_point_ = out;
    return true;
  }
  if (fd_ < 0) return false;
  char buf[256];
  const ssize_t n = read(fd_, buf, sizeof(buf));
  if (n > 0) read_buffer_.append(buf, static_cast<size_t>(n));
  size_t nl;
  while ((nl = read_buffer_.find('\n')) != std::string::npos) {
    const std::string line = read_buffer_.substr(0, nl);
    read_buffer_.erase(0, nl + 1);
    if (ParseLine(line, out)) { last_point_ = out; return true; }
  }
  return false;
}
#endif

void Digitizer::FeedSimulatedPoint(kernel::Point3d p, int button) {
  DigitizerPoint dp;
  dp.raw = p;
  dp.button = button;
  dp.valid = true;
  simulated_pending_ = dp;
}

kernel::Point3d Digitizer::ToModel(kernel::Point3d raw) const {
  // DigScale (device -> model unit scale) applies to the raw device
  // reading, before calibration - e.g. a caliper-style digitizer reporting
  // inches into a millimetre model.
  const kernel::Point3d scaled(raw.x * unit_scale_, raw.y * unit_scale_, raw.z * unit_scale_);
  if (!calibration_.set) return scaled;
  // Express `scaled` in the calibrated (origin, x_axis, y_axis) frame,
  // then rebuild it in the target frame - a rigid change of basis solved
  // from the 3 DigCalibrate points, not a general affine fit.
  const kernel::Vector3d rel = scaled - calibration_.origin;
  kernel::Vector3d x = calibration_.x_axis, y = calibration_.y_axis;
  if (!x.Unitize() || !y.Unitize()) return scaled;
  kernel::Vector3d z = ON_CrossProduct(x, y);
  if (!z.Unitize()) return scaled;
  y = ON_CrossProduct(z, x);
  const double u = ON_DotProduct(rel, x), v = ON_DotProduct(rel, y), w = ON_DotProduct(rel, z);
  kernel::Vector3d tx = calibration_.target_x, ty = calibration_.target_y;
  if (!tx.Unitize() || !ty.Unitize()) return scaled;
  kernel::Vector3d tz = ON_CrossProduct(tx, ty);
  if (!tz.Unitize()) return scaled;
  ty = ON_CrossProduct(tz, tx);
  return calibration_.target_origin + tx * u + ty * v + tz * w;
}

}  // namespace dino8::app
