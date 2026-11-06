#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace ps2br {

// Raw LZ4 block decode, which is the compression used by ZISO/ZSO images.
// This is not the LZ4 frame format. Trailing padding in `source` is ignored
// once `destination` is full.
bool decompressLz4Block(std::span<const std::byte> source,
                        std::span<std::byte> destination,
                        std::string& error);

} // namespace ps2br
