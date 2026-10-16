#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phys::cli {

// Writes an 8-bit RGBA PNG (uncompressed deflate, no dependencies). Returns false on I/O error.
bool WritePngRgba8(const std::string& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba);

} // namespace phys::cli
