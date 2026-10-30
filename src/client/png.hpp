#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace railmaster::client {

// Write an RGB image (row by row from the top) as a PNG. Returns false if
// the file cannot be written.
bool write_png(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb);

} // namespace railmaster::client
