// A small, dependency-free SHA-256 implementation (FIPS 180-4). Used by the
// plug-in marketplace (src/plugins/Marketplace.cpp) to verify a downloaded
// plug-in library against the index's optional "sha256" field before it is
// ever dlopen()'d - the only integrity check available for a plug-in
// fetched over plain download_url, since there is no code-signing here
// (see docs/CODE_SIGNING.md, which only covers the installer/binaries Dino
// 8 itself ships).
#pragma once

#include <string>

namespace dino8::util {

// Lower-case hex SHA-256 digest of `data`.
std::string Sha256Hex(const std::string& data);

// Lower-case hex SHA-256 digest of the file at `path`. Returns false (with
// `error` set) if the file cannot be opened.
bool Sha256HexOfFile(const std::string& path, std::string& out, std::string& error);

}  // namespace dino8::util
