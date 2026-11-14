// See Xlsx.h.
#include "drafting/Xlsx.h"

#include "util/Inflate.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace dino8::app::drafting {

namespace {

using dino8::util::Crc32;
using dino8::util::InflateRaw;

// ---------------------------------------------------------------------------
// Little-endian byte packing/unpacking (the ZIP format's own byte order).
// ---------------------------------------------------------------------------

void PutLE16(std::vector<unsigned char>& out, unsigned v) { out.push_back(static_cast<unsigned char>(v & 0xff)); out.push_back(static_cast<unsigned char>((v >> 8) & 0xff)); }
void PutLE32(std::vector<unsigned char>& out, uint32_t v) {
  out.push_back(static_cast<unsigned char>(v & 0xff));
  out.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
  out.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
  out.push_back(static_cast<unsigned char>((v >> 24) & 0xff));
}
unsigned GetLE16(const unsigned char* p) { return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8); }
uint32_t GetLE32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void AppendBytes(std::vector<unsigned char>& out, const std::string& s) { out.insert(out.end(), s.begin(), s.end()); }

// ---------------------------------------------------------------------------
// A minimal ZIP container - just enough to build/read the handful of small
// XML parts an OOXML workbook is made of. Not a general-purpose zip
// library: no directory entries, no multi-disk archives, no data
// descriptors on write (every size is known upfront, so none are needed).
// ---------------------------------------------------------------------------

struct ZipEntry { std::string name; std::vector<unsigned char> data; };

// Every entry stored uncompressed (method 0) - see Xlsx.h's own comment on
// why this is still a fully valid, universally-openable .xlsx.
std::vector<unsigned char> BuildZip(const std::vector<ZipEntry>& entries) {
  std::vector<unsigned char> out;
  struct CdRec { std::string name; uint32_t crc, size, offset; };
  std::vector<CdRec> cd;
  cd.reserve(entries.size());
  for (const ZipEntry& e : entries) {
    const uint32_t crc = Crc32(e.data.data(), e.data.size());
    const uint32_t offset = static_cast<uint32_t>(out.size());
    const uint32_t size = static_cast<uint32_t>(e.data.size());
    PutLE32(out, 0x04034b50);               // local file header signature
    PutLE16(out, 20);                        // version needed to extract
    PutLE16(out, 0);                         // general purpose bit flag
    PutLE16(out, 0);                         // compression method: store
    PutLE16(out, 0);                         // last mod file time
    PutLE16(out, 0);                         // last mod file date (0 => 1980-01-01, the DOS date epoch)
    PutLE32(out, crc);
    PutLE32(out, size);                      // compressed size
    PutLE32(out, size);                      // uncompressed size
    PutLE16(out, static_cast<unsigned>(e.name.size()));
    PutLE16(out, 0);                         // extra field length
    AppendBytes(out, e.name);
    out.insert(out.end(), e.data.begin(), e.data.end());
    cd.push_back({e.name, crc, size, offset});
  }
  const uint32_t cd_start = static_cast<uint32_t>(out.size());
  for (const CdRec& r : cd) {
    PutLE32(out, 0x02014b50);                // central file header signature
    PutLE16(out, 20);                         // version made by
    PutLE16(out, 20);                         // version needed to extract
    PutLE16(out, 0);                          // general purpose bit flag
    PutLE16(out, 0);                          // compression method
    PutLE16(out, 0);                          // last mod time
    PutLE16(out, 0);                          // last mod date
    PutLE32(out, r.crc);
    PutLE32(out, r.size);                     // compressed size
    PutLE32(out, r.size);                     // uncompressed size
    PutLE16(out, static_cast<unsigned>(r.name.size()));
    PutLE16(out, 0);                          // extra field length
    PutLE16(out, 0);                          // file comment length
    PutLE16(out, 0);                          // disk number start
    PutLE16(out, 0);                          // internal file attributes
    PutLE32(out, static_cast<uint32_t>(0100644u) << 16);  // external file attributes: a regular file, rw-r--r--
    PutLE32(out, r.offset);                   // relative offset of local header
    AppendBytes(out, r.name);
  }
  const uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_start;
  PutLE32(out, 0x06054b50);                  // end of central directory signature
  PutLE16(out, 0);                            // number of this disk
  PutLE16(out, 0);                            // disk where central directory starts
  PutLE16(out, static_cast<unsigned>(cd.size()));  // central directory records on this disk
  PutLE16(out, static_cast<unsigned>(cd.size()));  // total central directory records
  PutLE32(out, cd_size);
  PutLE32(out, cd_start);
  PutLE16(out, 0);                            // comment length
  return out;
}

// Finds `name` among the zip's central directory entries and returns its
// (decompressed, CRC-verified) data. Supports compression method 0 (store)
// and 8 (deflate, via InflateRaw) - anything else is a reported error, not
// a silent skip.
bool FindZipEntry(const std::vector<unsigned char>& zip, const std::string& name, std::vector<unsigned char>& data, std::string& error) {
  if (zip.size() < 22) { error = "not a zip file (too short)"; return false; }
  // The end-of-central-directory record sits at the very end, optionally
  // followed only by a (here, always empty on write, but on a read of a
  // real-world file possibly non-empty) comment - scan backward for its
  // signature within the standard max comment length.
  const size_t scan_from = zip.size() >= 22 ? zip.size() - 22 : 0;
  const size_t scan_limit = zip.size() > 65557 ? zip.size() - 65557 : 0;
  size_t eocd = std::string::npos;
  for (size_t i = scan_from + 1; i-- > scan_limit;) {
    if (GetLE32(&zip[i]) == 0x06054b50u) { eocd = i; break; }
    if (i == 0) break;
  }
  if (eocd == std::string::npos) { error = "not a zip file (no end-of-central-directory record)"; return false; }
  const unsigned entry_count = GetLE16(&zip[eocd + 10]);
  const uint32_t cd_off = GetLE32(&zip[eocd + 16]);
  size_t pos = cd_off;
  for (unsigned i = 0; i < entry_count; ++i) {
    if (pos + 46 > zip.size() || GetLE32(&zip[pos]) != 0x02014b50u) { error = "corrupt zip central directory"; return false; }
    const unsigned method = GetLE16(&zip[pos + 10]);
    const uint32_t crc = GetLE32(&zip[pos + 16]);
    const uint32_t csize = GetLE32(&zip[pos + 20]);
    const uint32_t usize = GetLE32(&zip[pos + 24]);
    const unsigned name_len = GetLE16(&zip[pos + 28]);
    const unsigned extra_len = GetLE16(&zip[pos + 30]);
    const unsigned comment_len = GetLE16(&zip[pos + 32]);
    const uint32_t lho = GetLE32(&zip[pos + 42]);
    if (pos + 46 + name_len > zip.size()) { error = "corrupt zip central directory"; return false; }
    const std::string entry_name(reinterpret_cast<const char*>(&zip[pos + 46]), name_len);
    pos += 46 + name_len + extra_len + comment_len;
    if (entry_name != name) continue;
    if (lho + 30 > zip.size() || GetLE32(&zip[lho]) != 0x04034b50u) { error = "corrupt local file header for '" + name + "'"; return false; }
    const unsigned lname_len = GetLE16(&zip[lho + 26]);
    const unsigned lextra_len = GetLE16(&zip[lho + 28]);
    const size_t data_off = lho + 30 + lname_len + lextra_len;
    if (data_off + csize > zip.size()) { error = "truncated entry '" + name + "'"; return false; }
    const unsigned char* dptr = &zip[data_off];
    data.clear();
    if (method == 0) {
      data.assign(dptr, dptr + csize);
    } else if (method == 8) {
      // Cap decompression at the entry's own declared uncompressed size
      // (checked again below), but clamped to a sane ceiling first: `usize`
      // is itself an attacker-controlled header field, not a verified
      // expectation, so passing it through unclamped just lets a crafted
      // local/central-directory pair declare e.g. a 4 GiB uncompressed size
      // and have a few-hundred-KB back-reference-heavy deflate stream (a
      // classic zip-bomb ratio, ~1000:1 is easy within a single stream)
      // actually grow `data` to gigabytes before this function ever gets a
      // chance to reject it on the size/CRC check below.
      const size_t cap = std::min<size_t>(usize, dino8::util::kDefaultMaxInflateOutput);
      if (!InflateRaw(dptr, csize, data, cap)) { error = "corrupt deflate stream in '" + name + "'"; return false; }
    } else {
      error = "unsupported zip compression method (" + std::to_string(method) + ") in '" + name + "'";
      return false;
    }
    if (data.size() != usize || Crc32(data.data(), data.size()) != crc) { error = "'" + name + "' failed its CRC-32/size check (corrupt file)"; return false; }
    return true;
  }
  error = "no '" + name + "' entry in the zip";
  return false;
}

// ---------------------------------------------------------------------------
// A tiny, structure-aware scanner for the handful of XML shapes
// spreadsheetML actually needs (row > cell > value; si > t; sheet;
// Relationship) - not a general XML parser. It relies on none of these tag
// names ever nesting within an element of the same name, which is true
// throughout spreadsheetML/OPC relationship XML.
// ---------------------------------------------------------------------------

std::string AttrOf(const std::string& start_tag, const std::string& attr) {
  const std::string needle = attr + "=\"";
  const size_t p = start_tag.find(needle);
  if (p == std::string::npos) return "";
  const size_t v = p + needle.size();
  const size_t e = start_tag.find('"', v);
  if (e == std::string::npos) return "";
  return start_tag.substr(v, e - v);
}

// Finds the next <name ...>...</name> (or self-closing <name .../>)
// element at/after `pos`. On success, `start_tag` is the opening tag's full
// text (for AttrOf), `inner` is its content (empty for self-closing), and
// `next_pos` is where to resume scanning for a sibling.
bool NextElement(const std::string& xml, size_t pos, const std::string& name, std::string& start_tag, std::string& inner, size_t& next_pos) {
  const std::string open_needle = "<" + name;
  for (;;) {
    const size_t p = xml.find(open_needle, pos);
    if (p == std::string::npos) return false;
    const size_t after = p + open_needle.size();
    // Reject a same-prefix longer tag name (e.g. "<row" must not match "<rowspan").
    if (after < xml.size() && xml[after] != ' ' && xml[after] != '>' && xml[after] != '/' && xml[after] != '\t' && xml[after] != '\n' && xml[after] != '\r') { pos = p + 1; continue; }
    const size_t tag_end = xml.find('>', after);
    if (tag_end == std::string::npos) return false;
    const bool self_closing = tag_end > 0 && xml[tag_end - 1] == '/';
    start_tag = xml.substr(p, tag_end - p + 1);
    if (self_closing) { inner.clear(); next_pos = tag_end + 1; return true; }
    const std::string close_needle = "</" + name + ">";
    const size_t close_pos = xml.find(close_needle, tag_end + 1);
    if (close_pos == std::string::npos) return false;
    inner = xml.substr(tag_end + 1, close_pos - (tag_end + 1));
    next_pos = close_pos + close_needle.size();
    return true;
  }
}

void AppendUtf8(std::string& out, unsigned code) {
  if (code <= 0x7F) { out += static_cast<char>(code); return; }
  if (code <= 0x7FF) {
    out += static_cast<char>(0xC0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3F));
    return;
  }
  if (code <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
    return;
  }
  out += static_cast<char>(0xF0 | (code >> 18));
  out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
  out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
  out += static_cast<char>(0x80 | (code & 0x3F));
}

std::string XmlUnescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '&') {
      const size_t semi = s.find(';', i);
      if (semi != std::string::npos && semi - i <= 12) {
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") { out += '&'; i = semi + 1; continue; }
        if (ent == "lt") { out += '<'; i = semi + 1; continue; }
        if (ent == "gt") { out += '>'; i = semi + 1; continue; }
        if (ent == "quot") { out += '"'; i = semi + 1; continue; }
        if (ent == "apos") { out += '\''; i = semi + 1; continue; }
        if (ent.size() > 1 && ent[0] == '#') {
          const bool hex = ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X');
          const long code = std::strtol(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
          if (code > 0 && code <= 0x10FFFF) { AppendUtf8(out, static_cast<unsigned>(code)); i = semi + 1; continue; }
        }
      }
    }
    out += s[i];
    ++i;
  }
  return out;
}

std::string XmlEscapeText(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '\r': out += "&#13;"; break;  // avoid CR/LF normalization surprises on re-read
      default: out += c;
    }
  }
  return out;
}

// 0-based column index -> spreadsheet letters ("A", "B", ..., "Z", "AA", ...).
std::string ColumnLetters(int col0) {
  std::string s;
  long c = col0 + 1;
  while (c > 0) {
    const long rem = (c - 1) % 26;
    s.insert(s.begin(), static_cast<char>('A' + rem));
    c = (c - 1) / 26;
  }
  return s;
}

// The real OOXML worksheet limits (XFD1048576), used to reject absurd cell
// references before they ever reach an int: without this, a column ref with
// enough letters (e.g. 7+ Z's) overflows `long` on the multiply-by-26 below,
// and the subsequent narrowing to int can land on a negative col0 that then
// bypasses ParseSheetRows's `row.size() <= cc` bounds check and corrupts the
// heap via an out-of-bounds `row[cc]` write.
constexpr long kMaxXlsxColumn = 16384;
constexpr long kMaxXlsxRow = 1048576;

// A cell reference like "AB12" -> (row0, col0), both 0-based. False for an
// unparseable reference (no leading letters, no trailing digits, or either
// coordinate outside the real worksheet limits above).
bool ParseCellRef(const std::string& ref, int& row0, int& col0) {
  size_t i = 0;
  long col = 0;
  while (i < ref.size() && std::isalpha(static_cast<unsigned char>(ref[i]))) {
    col = col * 26 + (std::toupper(static_cast<unsigned char>(ref[i])) - 'A' + 1);
    if (col > kMaxXlsxColumn) return false;
    ++i;
  }
  if (i == 0 || i >= ref.size()) return false;
  const long row = std::atol(ref.c_str() + i);
  if (row <= 0 || col <= 0 || row > kMaxXlsxRow) return false;
  row0 = static_cast<int>(row - 1);
  col0 = static_cast<int>(col - 1);
  return true;
}

// ---------------------------------------------------------------------------
// The fixed, minimal OOXML package parts a one-sheet workbook needs.
// ---------------------------------------------------------------------------

const char* kContentTypesXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
    "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
    "</Types>";

const char* kRootRelsXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
    "</Relationships>";

const char* kWorkbookXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
    "<sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
    "</workbook>";

const char* kWorkbookRelsXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
    "</Relationships>";

std::string BuildSheetXml(int rows, int cols, const std::vector<std::string>& cells) {
  std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                     "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>";
  for (int r = 0; r < rows; ++r) {
    xml += "<row r=\"" + std::to_string(r + 1) + "\">";
    for (int c = 0; c < cols; ++c) {
      const std::string& v = cells[static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c)];
      if (v.empty()) continue;  // sparse: an empty cell is simply not written, standard OOXML practice
      xml += "<c r=\"" + ColumnLetters(c) + std::to_string(r + 1) + "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">" + XmlEscapeText(v) + "</t></is></c>";
    }
    xml += "</row>";
  }
  xml += "</sheetData></worksheet>";
  return xml;
}

std::vector<std::string> ParseSharedStrings(const std::string& xml) {
  std::vector<std::string> out;
  size_t pos = 0;
  std::string start_tag, inner;
  size_t next = 0;
  while (NextElement(xml, pos, "si", start_tag, inner, next)) {
    pos = next;
    // Concatenate every <t> inside - a bare <t>text</t> or rich-text
    // <r><t>run</t></r>... runs both end up with just their text content.
    std::string text;
    size_t p2 = 0;
    std::string st2, in2;
    size_t nx2 = 0;
    while (NextElement(inner, p2, "t", st2, in2, nx2)) { text += XmlUnescape(in2); p2 = nx2; }
    out.push_back(text);
  }
  return out;
}

void ParseSheetRows(const std::string& xml, const std::vector<std::string>& shared, std::vector<std::vector<std::string>>& rows) {
  size_t pos = 0;
  std::string rtag, rinner;
  size_t rnext = 0;
  while (NextElement(xml, pos, "row", rtag, rinner, rnext)) {
    pos = rnext;
    const std::string rattr = AttrOf(rtag, "r");
    // atol (not atoi): a huge digit string would overflow int in atoi's
    // return type before the range check below ever sees it. A row index
    // past the real worksheet limit is rejected rather than accepted, which
    // would otherwise resize `rows` to an attacker-chosen multi-gigabyte size.
    const long rval = rattr.empty() ? -1 : std::atol(rattr.c_str());
    int row0 = (rval <= 0 || rval > kMaxXlsxRow) ? -1 : static_cast<int>(rval - 1);
    if (row0 < 0) continue;  // malformed or out-of-range row index - skip rather than guess
    if (static_cast<int>(rows.size()) <= row0) rows.resize(static_cast<size_t>(row0) + 1);
    size_t cpos = 0;
    std::string ctag, cinner;
    size_t cnext = 0;
    while (NextElement(rinner, cpos, "c", ctag, cinner, cnext)) {
      cpos = cnext;
      int rr = 0, cc = 0;
      if (!ParseCellRef(AttrOf(ctag, "r"), rr, cc)) continue;
      const std::string t = AttrOf(ctag, "t");
      std::string value;
      if (t == "inlineStr") {
        size_t p3 = 0;
        std::string ist, isi;
        size_t isn = 0;
        if (NextElement(cinner, p3, "is", ist, isi, isn)) {
          size_t p4 = 0;
          std::string tt, ti;
          size_t tn = 0;
          while (NextElement(isi, p4, "t", tt, ti, tn)) { value += XmlUnescape(ti); p4 = tn; }
        }
      } else {
        size_t p3 = 0;
        std::string vt, vi;
        size_t vn = 0;
        if (NextElement(cinner, p3, "v", vt, vi, vn)) {
          if (t == "s") {
            const int idx = std::atoi(vi.c_str());
            if (idx >= 0 && idx < static_cast<int>(shared.size())) value = shared[static_cast<size_t>(idx)];
          } else if (t == "b") {
            value = (vi == "1") ? "TRUE" : "FALSE";
          } else {
            value = XmlUnescape(vi);  // numeric (no t) or t="str" (formula string result)
          }
        }
      }
      std::vector<std::string>& row = rows[static_cast<size_t>(row0)];
      if (static_cast<int>(row.size()) <= cc) row.resize(static_cast<size_t>(cc) + 1);
      row[static_cast<size_t>(cc)] = value;
    }
  }
}

}  // namespace

bool WriteXlsxCells(const std::string& path, int rows, int cols, const std::vector<std::string>& cells, std::string& error) {
  if (rows <= 0 || cols <= 0 || cells.size() != static_cast<size_t>(rows) * static_cast<size_t>(cols)) { error = "nothing to write (empty table)"; return false; }
  std::vector<ZipEntry> entries = {
      {"[Content_Types].xml", std::vector<unsigned char>(kContentTypesXml, kContentTypesXml + std::strlen(kContentTypesXml))},
      {"_rels/.rels", std::vector<unsigned char>(kRootRelsXml, kRootRelsXml + std::strlen(kRootRelsXml))},
      {"xl/workbook.xml", std::vector<unsigned char>(kWorkbookXml, kWorkbookXml + std::strlen(kWorkbookXml))},
      {"xl/_rels/workbook.xml.rels", std::vector<unsigned char>(kWorkbookRelsXml, kWorkbookRelsXml + std::strlen(kWorkbookRelsXml))},
  };
  const std::string sheet_xml = BuildSheetXml(rows, cols, cells);
  entries.push_back({"xl/worksheets/sheet1.xml", std::vector<unsigned char>(sheet_xml.begin(), sheet_xml.end())});
  const std::vector<unsigned char> zip = BuildZip(entries);
  // Binary mode, matching WriteCsvFile's own choice (cmd_drafting2.cpp) -
  // a zip's bytes must never go through a text-mode CRLF translation.
  std::ofstream f(path, std::ios::trunc | std::ios::binary);
  if (!f) { error = "could not open '" + path + "' for writing"; return false; }
  f.write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));
  if (!f) { error = "write failed for '" + path + "'"; return false; }
  return true;
}

bool ReadXlsxCells(const std::string& path, std::vector<std::vector<std::string>>& rows, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { error = "could not open '" + path + "'"; return false; }
  std::vector<unsigned char> zip((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (zip.empty()) { error = "'" + path + "' is empty"; return false; }

  std::vector<unsigned char> wb_bytes;
  if (!FindZipEntry(zip, "xl/workbook.xml", wb_bytes, error)) return false;
  const std::string workbook(wb_bytes.begin(), wb_bytes.end());

  // Resolve the first <sheet r:id=.../> via workbook.xml.rels' own
  // Relationship graph, not a "sheet1.xml" filename guess - see Xlsx.h.
  std::string first_rid;
  {
    size_t pos = 0;
    std::string stag, sinner;
    size_t snext = 0;
    if (NextElement(workbook, pos, "sheets", stag, sinner, snext)) {
      size_t p2 = 0;
      std::string sh_tag, sh_inner;
      size_t sh_next = 0;
      if (NextElement(sinner, p2, "sheet", sh_tag, sh_inner, sh_next)) first_rid = AttrOf(sh_tag, "r:id");
    }
  }
  std::string sheet_path = "xl/worksheets/sheet1.xml";  // fallback if the rels can't be resolved
  if (!first_rid.empty()) {
    std::vector<unsigned char> rels_bytes;
    std::string rerr;
    if (FindZipEntry(zip, "xl/_rels/workbook.xml.rels", rels_bytes, rerr)) {
      const std::string rels(rels_bytes.begin(), rels_bytes.end());
      size_t pos = 0;
      std::string rtag, rinner;
      size_t rnext = 0;
      while (NextElement(rels, pos, "Relationship", rtag, rinner, rnext)) {
        pos = rnext;
        if (AttrOf(rtag, "Id") == first_rid) {
          const std::string target = AttrOf(rtag, "Target");
          if (!target.empty()) sheet_path = (target[0] == '/') ? target.substr(1) : "xl/" + target;
          break;
        }
      }
    }
  }

  std::vector<unsigned char> sheet_bytes;
  if (!FindZipEntry(zip, sheet_path, sheet_bytes, error)) return false;
  const std::string sheet_xml(sheet_bytes.begin(), sheet_bytes.end());

  std::vector<std::string> shared;
  {
    std::vector<unsigned char> shared_bytes;
    std::string serr;
    if (FindZipEntry(zip, "xl/sharedStrings.xml", shared_bytes, serr)) shared = ParseSharedStrings(std::string(shared_bytes.begin(), shared_bytes.end()));
  }

  rows.clear();
  ParseSheetRows(sheet_xml, shared, rows);
  if (rows.empty()) { error = "'" + path + "' has no readable rows"; return false; }
  return true;
}

}  // namespace dino8::app::drafting
