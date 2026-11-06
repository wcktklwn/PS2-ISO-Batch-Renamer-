#include "core/Lz4.h"

#include <cstdint>

namespace ps2br {
namespace {

std::uint8_t asByte(std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

bool extendLength(std::size_t& length,
                  std::span<const std::byte> source,
                  std::size_t& cursor,
                  std::size_t limit,
                  std::string& error) {
    if (length > limit) {
        error = "LZ4 block length exceeds the output size";
        return false;
    }
    while (true) {
        if (cursor >= source.size()) {
            error = "LZ4 block ended during a length extension";
            return false;
        }
        const std::uint8_t extra = asByte(source[cursor]);
        ++cursor;
        if (extra > limit - length) {
            error = "LZ4 block length exceeds the output size";
            return false;
        }
        length += extra;
        if (extra != 255) {
            return true;
        }
    }
}

} // namespace

bool decompressLz4Block(std::span<const std::byte> source,
                        std::span<std::byte> destination,
                        std::string& error) {
    std::size_t cursor = 0;
    std::size_t written = 0;

    while (written < destination.size()) {
        if (cursor >= source.size()) {
            error = "LZ4 block ended before the output was complete";
            return false;
        }

        const std::uint8_t token = asByte(source[cursor]);
        ++cursor;

        std::size_t literal_length = token >> 4U;
        if (literal_length == 15 &&
            !extendLength(literal_length, source, cursor, destination.size() - written, error)) {
            return false;
        }
        if (literal_length > destination.size() - written || literal_length > source.size() - cursor) {
            error = "LZ4 literals extend outside the block";
            return false;
        }
        for (std::size_t index = 0; index < literal_length; ++index) {
            destination[written + index] = source[cursor + index];
        }
        cursor += literal_length;
        written += literal_length;
        if (written == destination.size()) {
            return true;
        }

        if (source.size() - cursor < 2) {
            error = "LZ4 block ended before a match offset";
            return false;
        }
        const std::size_t offset = static_cast<std::size_t>(asByte(source[cursor])) |
                                   (static_cast<std::size_t>(asByte(source[cursor + 1])) << 8U);
        cursor += 2;
        if (offset == 0 || offset > written) {
            error = "LZ4 match offset is outside the decoded output";
            return false;
        }

        std::size_t match_length = static_cast<std::size_t>(token & 0x0FU) + 4U;
        if ((token & 0x0FU) == 15 &&
            !extendLength(match_length, source, cursor, destination.size() - written, error)) {
            return false;
        }
        if (match_length > destination.size() - written) {
            error = "LZ4 match extends outside the output";
            return false;
        }
        for (std::size_t index = 0; index < match_length; ++index) {
            destination[written] = destination[written - offset];
            ++written;
        }
    }

    return true;
}

} // namespace ps2br
