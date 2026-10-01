// Unit test for the .xlsx (OOXML SpreadsheetML) codec (drafting/Xlsx.cpp's
// WriteXlsxCells/ReadXlsxCells - see PARITY_MAP.md's "Live external data
// linking into tables" entry): a real write/read round trip through this
// app's own writer (special characters, empty cells, several rows/cols),
// a hand-built "real-world-shaped" workbook - a non-"sheet1.xml" worksheet
// part name resolved through the real relationship graph, a deflate-
// compressed (zip method 8) worksheet entry, and cells referencing a
// shared-strings table by index (t="s") - the shape an actual Excel/
// LibreOffice/openpyxl save produces, not just this app's own inlineStr
// writer, and the malformed/truncated/bad-CRC error paths.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "drafting/Xlsx.h"

using dino8::app::drafting::ReadXlsxCells;
using dino8::app::drafting::WriteXlsxCells;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

bool WriteFile(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(f);
}

// ---------------------------------------------------------------------------
// A tiny, independent ZIP builder for hand-crafting fixture files - kept
// deliberately separate from drafting/Xlsx.cpp's own (private, anonymous-
// namespace) BuildZip, the same "duplicate a small amount of encoding logic
// to build a fixture, rather than exposing test-only surface from the
// module under test" approach tests/test_image_hdr.cpp already uses for its
// own hand-built RLE scanline.
// ---------------------------------------------------------------------------

void PutLE16(std::vector<unsigned char>& out, unsigned v) { out.push_back(static_cast<unsigned char>(v & 0xff)); out.push_back(static_cast<unsigned char>((v >> 8) & 0xff)); }
void PutLE32(std::vector<unsigned char>& out, uint32_t v) {
  out.push_back(static_cast<unsigned char>(v & 0xff));
  out.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
  out.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
  out.push_back(static_cast<unsigned char>((v >> 24) & 0xff));
}

uint32_t Crc32Local(const unsigned char* data, size_t n) {
  static uint32_t table[256];
  static bool built = false;
  if (!built) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    built = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

std::vector<unsigned char> ToBytes(const std::string& s) { return std::vector<unsigned char>(s.begin(), s.end()); }

// A single deflate (RFC 1951) "stored" block (BFINAL=1, BTYPE=00) wrapping
// `data` verbatim - a real, valid deflate bit-stream (no Huffman coding
// needed for this to be genuine, spec-conformant method-8 data), the same
// technique render/ImageIO.cpp's own PNG writer uses for its "(uncompressed)
// deflate block writer, RFC 1951" (see that file's own comment).
std::vector<unsigned char> StoredDeflateBlock(const std::string& data) {
  std::vector<unsigned char> out;
  out.push_back(0x01);  // BFINAL=1, BTYPE=00 (stored), rest of the byte is padding
  const unsigned len = static_cast<unsigned>(data.size());
  PutLE16(out, len);
  PutLE16(out, len ^ 0xffffu);
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

struct FixtureEntry { std::string name; std::vector<unsigned char> data; unsigned method; };

// method 0 (store) or 8 (deflate - `data` must already be a valid deflate
// bit-stream, e.g. from StoredDeflateBlock, and the entry's own declared
// "uncompressed size" is `orig_size`, separate from `data`'s own length).
std::vector<unsigned char> BuildZipFixture(const std::vector<FixtureEntry>& entries, const std::vector<std::string>& orig_texts) {
  std::vector<unsigned char> out;
  struct CdRec { std::string name; uint32_t crc, csize, usize, offset; unsigned method; };
  std::vector<CdRec> cd;
  for (size_t idx = 0; idx < entries.size(); ++idx) {
    const FixtureEntry& e = entries[idx];
    const std::string& orig = orig_texts[idx];
    const uint32_t crc = Crc32Local(reinterpret_cast<const unsigned char*>(orig.data()), orig.size());
    const uint32_t offset = static_cast<uint32_t>(out.size());
    PutLE32(out, 0x04034b50);
    PutLE16(out, 20);
    PutLE16(out, 0);
    PutLE16(out, e.method);
    PutLE16(out, 0);
    PutLE16(out, 0);
    PutLE32(out, crc);
    PutLE32(out, static_cast<uint32_t>(e.data.size()));
    PutLE32(out, static_cast<uint32_t>(orig.size()));
    PutLE16(out, static_cast<unsigned>(e.name.size()));
    PutLE16(out, 0);
    out.insert(out.end(), e.name.begin(), e.name.end());
    out.insert(out.end(), e.data.begin(), e.data.end());
    cd.push_back({e.name, crc, static_cast<uint32_t>(e.data.size()), static_cast<uint32_t>(orig.size()), offset, e.method});
  }
  const uint32_t cd_start = static_cast<uint32_t>(out.size());
  for (const CdRec& r : cd) {
    PutLE32(out, 0x02014b50);
    PutLE16(out, 20); PutLE16(out, 20); PutLE16(out, 0); PutLE16(out, r.method);
    PutLE16(out, 0); PutLE16(out, 0);
    PutLE32(out, r.crc);
    PutLE32(out, r.csize);
    PutLE32(out, r.usize);
    PutLE16(out, static_cast<unsigned>(r.name.size()));
    PutLE16(out, 0); PutLE16(out, 0); PutLE16(out, 0); PutLE16(out, 0);
    PutLE32(out, 0);
    PutLE32(out, r.offset);
    out.insert(out.end(), r.name.begin(), r.name.end());
  }
  const uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_start;
  PutLE32(out, 0x06054b50);
  PutLE16(out, 0); PutLE16(out, 0);
  PutLE16(out, static_cast<unsigned>(cd.size()));
  PutLE16(out, static_cast<unsigned>(cd.size()));
  PutLE32(out, cd_size);
  PutLE32(out, cd_start);
  PutLE16(out, 0);
  return out;
}

// A minimal, valid package wrapping a hand-written worksheet XML - for
// fixtures that only care about exercising ParseSheetRows/ParseCellRef on a
// crafted <sheetData>, not the rels-graph/shared-strings machinery the
// "real-world-shaped" fixture above already covers.
std::vector<unsigned char> BuildMinimalXlsx(const std::string& worksheet_xml) {
  const std::string content_types =
      "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
      "<Default Extension=\"xml\" ContentType=\"application/xml\"/></Types>";
  const std::string root_rels =
      "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
      "</Relationships>";
  const std::string workbook =
      "<?xml version=\"1.0\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
      "<sheets><sheet name=\"Data\" sheetId=\"1\" r:id=\"rIdSheet\"/></sheets></workbook>";
  const std::string workbook_rels =
      "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rIdSheet\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
      "</Relationships>";
  std::vector<FixtureEntry> entries = {
      {"[Content_Types].xml", ToBytes(content_types), 0},
      {"_rels/.rels", ToBytes(root_rels), 0},
      {"xl/workbook.xml", ToBytes(workbook), 0},
      {"xl/_rels/workbook.xml.rels", ToBytes(workbook_rels), 0},
      {"xl/worksheets/sheet1.xml", ToBytes(worksheet_xml), 0},
  };
  std::vector<std::string> orig_texts = {content_types, root_rels, workbook, workbook_rels, worksheet_xml};
  return BuildZipFixture(entries, orig_texts);
}

}  // namespace

int main() {
  const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "dino8_test_xlsx";
  std::filesystem::create_directories(tmp);

  // ---- Round trip through this app's own writer -------------------------
  {
    const int rows = 3, cols = 3;
    const std::vector<std::string> cells = {
        "Hello, World", "3.14159", "",
        "Quote\"Amp&Lt<Gt>", "007", "line1\nline2",
        "", "trailing space ", " leading space",
    };
    const std::string path = (tmp / "roundtrip.xlsx").string();
    std::string err;
    Check(WriteXlsxCells(path, rows, cols, cells, err), "WriteXlsxCells writes a round-trip fixture");
    std::vector<std::vector<std::string>> out;
    Check(ReadXlsxCells(path, out, err), "ReadXlsxCells reads it back");
    bool match = out.size() == static_cast<size_t>(rows);
    if (match) {
      for (int r = 0; r < rows && match; ++r) {
        for (int c = 0; c < cols; ++c) {
          const std::string expect = cells[static_cast<size_t>(r) * cols + c];
          const std::string got = (c < static_cast<int>(out[static_cast<size_t>(r)].size())) ? out[static_cast<size_t>(r)][static_cast<size_t>(c)] : "";
          if (got != expect) { match = false; std::printf("     mismatch at (%d,%d): got \"%s\" want \"%s\"\n", r, c, got.c_str(), expect.c_str()); break; }
        }
      }
    }
    Check(match, "round trip preserves every cell exactly, including special characters, empty cells and leading/trailing whitespace");
  }

  // ---- Empty-grid rejection ----------------------------------------------
  {
    std::string err;
    Check(!WriteXlsxCells((tmp / "empty.xlsx").string(), 0, 0, {}, err), "WriteXlsxCells refuses an empty grid");
  }

  // ---- A hand-built "real-world-shaped" workbook: non-sheet1.xml part
  // name (resolved via workbook.xml.rels, not a filename guess), method-8
  // (deflate) worksheet entry, and shared strings referenced by index. ----
  {
    const std::string content_types =
        "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/></Types>";
    const std::string root_rels =
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";
    const std::string workbook =
        "<?xml version=\"1.0\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"Data\" sheetId=\"1\" r:id=\"rIdSheet\"/></sheets></workbook>";
    const std::string workbook_rels =
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rIdSheet\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/notSheet1.xml\"/>"
        "<Relationship Id=\"rIdStrings\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>"
        "</Relationships>";
    const std::string shared_strings =
        "<?xml version=\"1.0\"?><sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"2\" uniqueCount=\"2\">"
        "<si><t>Hello &amp; welcome</t></si><si><r><t>Ro</t></r><r><t>w Two</t></r></si></sst>";
    const std::string worksheet =
        "<?xml version=\"1.0\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\"><v>123.5</v></c></row>"
        "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>1</v></c><c r=\"B2\" t=\"inlineStr\"><is><t>inline text</t></is></c></row>"
        "</sheetData></worksheet>";

    std::vector<FixtureEntry> entries = {
        {"[Content_Types].xml", ToBytes(content_types), 0},
        {"_rels/.rels", ToBytes(root_rels), 0},
        {"xl/workbook.xml", ToBytes(workbook), 0},
        {"xl/_rels/workbook.xml.rels", ToBytes(workbook_rels), 0},
        {"xl/sharedStrings.xml", ToBytes(shared_strings), 0},
        {"xl/worksheets/notSheet1.xml", StoredDeflateBlock(worksheet), 8},
    };
    std::vector<std::string> orig_texts = {content_types, root_rels, workbook, workbook_rels, shared_strings, worksheet};
    const std::vector<unsigned char> zip = BuildZipFixture(entries, orig_texts);
    const std::string path = (tmp / "realworld.xlsx").string();
    Check(WriteFile(path, zip), "hand-built fixture written to disk");

    std::vector<std::vector<std::string>> out;
    std::string err;
    Check(ReadXlsxCells(path, out, err), "ReadXlsxCells reads a non-sheet1.xml, deflate-compressed, shared-string-indexed workbook");
    const bool shape_ok = out.size() == 2 && out[0].size() >= 2 && out[1].size() >= 2;
    Check(shape_ok, "the hand-built workbook has 2 rows x >=2 cols");
    if (shape_ok) {
      Check(out[0][0] == "Hello & welcome", "A1 resolves shared string 0 (plain <si><t>)");
      Check(out[0][1] == "123.5", "B1 reads a numeric cell's raw <v> text");
      Check(out[1][0] == "Row Two", "A2 resolves shared string 1 (rich-text <si><r><t>...</t></r> runs concatenated)");
      Check(out[1][1] == "inline text", "B2 reads an inlineStr cell");
    }
  }

  // ---- Error paths --------------------------------------------------------
  {
    std::vector<std::vector<std::string>> out;
    std::string err;
    Check(!ReadXlsxCells((tmp / "does_not_exist.xlsx").string(), out, err), "ReadXlsxCells reports a missing file");
    Check(!err.empty(), "  ...with a non-empty error message");
  }
  {
    const std::string path = (tmp / "not_a_zip.xlsx").string();
    Check(WriteFile(path, ToBytes("this is not a zip file at all")), "wrote a non-zip fixture");
    std::vector<std::vector<std::string>> out;
    std::string err;
    Check(!ReadXlsxCells(path, out, err), "ReadXlsxCells rejects a non-zip file");
  }
  {
    // A genuine round-tripped file with one data byte flipped must fail its
    // CRC-32 check rather than silently returning corrupted cell text.
    const int rows = 1, cols = 1;
    const std::string path = (tmp / "corrupt.xlsx").string();
    std::string err;
    Check(WriteXlsxCells(path, rows, cols, {"Some Data"}, err), "wrote a fixture to corrupt");
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    bool flipped = false;
    for (size_t i = bytes.size(); i-- > 0;) {
      if (bytes[i] == 'D') { bytes[i] = 'X'; flipped = true; break; }  // corrupt a byte inside "Some Data"'s own sheet XML
    }
    Check(flipped, "found a byte to flip inside the fixture");
    Check(WriteFile(path, bytes), "wrote the corrupted fixture back");
    std::vector<std::vector<std::string>> out;
    Check(!ReadXlsxCells(path, out, err), "ReadXlsxCells rejects a CRC-mismatched entry instead of returning corrupted text");
  }
  {
    std::string path = (tmp / "truncated.xlsx").string();
    std::string err;
    Check(WriteXlsxCells(path, 2, 2, {"a", "b", "c", "d"}, err), "wrote a fixture to truncate");
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    bytes.resize(bytes.size() / 2);
    Check(WriteFile(path, bytes), "wrote the truncated fixture");
    std::vector<std::vector<std::string>> out;
    Check(!ReadXlsxCells(path, out, err), "ReadXlsxCells rejects a truncated zip (no end-of-central-directory record)");
  }

  // ---- Untrusted-input hardening ------------------------------------------

  // A cell reference with enough column letters overflows `long` in
  // ParseCellRef's old unbounded version, and the subsequent narrowing to
  // int could land on a negative col0 that bypassed ParseSheetRows'
  // `row.size() <= cc` bounds check, corrupting the heap via an
  // out-of-bounds `row[cc]` write. The malicious cell must simply be
  // skipped (ParseCellRef now rejects anything past the real XFD16384
  // worksheet limit), not crash or corrupt the legitimate cell beside it.
  {
    const std::string worksheet =
        "<?xml version=\"1.0\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
        "<row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>ok</t></is></c>"
        "<c r=\"ZZZZZZZ1\" t=\"inlineStr\"><is><t>bad</t></is></c></row>"
        "</sheetData></worksheet>";
    const std::string path = (tmp / "overflow_col.xlsx").string();
    Check(WriteFile(path, BuildMinimalXlsx(worksheet)), "wrote an overflow-column fixture");
    std::vector<std::vector<std::string>> out;
    std::string err;
    Check(ReadXlsxCells(path, out, err), "ReadXlsxCells survives a cell ref with way too many column letters");
    Check(out.size() == 1 && !out[0].empty() && out[0][0] == "ok", "the legitimate A1 cell is intact");
    Check(out.size() == 1 && out[0].size() < 1000, "the malicious column ref was rejected, not used as a huge/negative index");
  }

  // A row index with no sane upper bound (an untrusted .3dm/.xlsx can claim
  // any 32-bit value) used to be resized straight into `rows`, letting a
  // few bytes of XML demand a multi-gigabyte allocation. It must be
  // rejected (skipped) instead, like any other out-of-range row.
  {
    const std::string worksheet =
        "<?xml version=\"1.0\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
        "<row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>ok</t></is></c></row>"
        "<row r=\"2000000000\"><c r=\"A2000000000\" t=\"inlineStr\"><is><t>bad</t></is></c></row>"
        "</sheetData></worksheet>";
    const std::string path = (tmp / "overflow_row.xlsx").string();
    Check(WriteFile(path, BuildMinimalXlsx(worksheet)), "wrote an overflow-row fixture");
    std::vector<std::vector<std::string>> out;
    std::string err;
    Check(ReadXlsxCells(path, out, err), "ReadXlsxCells survives a row index of 2,000,000,000 without a huge allocation");
    Check(out.size() == 1 && !out[0].empty() && out[0][0] == "ok", "only the legitimate row 1 is present; the absurd row index was rejected");
  }

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
