#include "core/ZsoReader.h"

#include "core/Lz4.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace ps2br {
namespace {

constexpr std::uint32_t kZsoHeaderSize = 24;
constexpr std::uint32_t kMaximumBlockSize = 1024U * 1024U;

std::uint8_t asByte(std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

std::uint32_t loadLe32(const std::byte* data) {
    return static_cast<std::uint32_t>(asByte(data[0])) |
           (static_cast<std::uint32_t>(asByte(data[1])) << 8U) |
           (static_cast<std::uint32_t>(asByte(data[2])) << 16U) |
           (static_cast<std::uint32_t>(asByte(data[3])) << 24U);
}

std::uint64_t loadLe64(const std::byte* data) {
    std::uint64_t value = 0;
    for (int index = 7; index >= 0; --index) {
        value = (value << 8U) | asByte(data[index]);
    }
    return value;
}

} // namespace

ZsoReader::ZsoReader(const std::filesystem::path& path)
    : file_(path, std::ios::binary) {
    if (!file_) {
        open_error_ = "Could not open the ZSO for reading";
        return;
    }

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec || file_size < kZsoHeaderSize) {
        open_error_ = ec ? "Could not determine the ZSO file size" : "ZSO header is incomplete";
        file_.close();
        return;
    }

    std::array<std::byte, kZsoHeaderSize> header{};
    file_.read(reinterpret_cast<char*>(header.data()), header.size());
    if (file_.gcount() != static_cast<std::streamsize>(header.size())) {
        open_error_ = "ZSO header is incomplete";
        file_.close();
        return;
    }

    if (asByte(header[0]) != 'Z' || asByte(header[1]) != 'I' ||
        asByte(header[2]) != 'S' || asByte(header[3]) != 'O') {
        open_error_ = "ZSO header is not a ZISO image";
        file_.close();
        return;
    }

    const std::uint32_t header_size = loadLe32(header.data() + 4);
    total_bytes_ = loadLe64(header.data() + 8);
    block_size_ = loadLe32(header.data() + 16);
    const std::uint8_t version = asByte(header[20]);
    align_ = asByte(header[21]);
    if (header_size != kZsoHeaderSize || version != 1 || block_size_ == 0 ||
        block_size_ > kMaximumBlockSize || align_ > 31 || total_bytes_ == 0) {
        open_error_ = "ZSO header is invalid";
        file_.close();
        return;
    }

    block_count_ = total_bytes_ / block_size_ + (total_bytes_ % block_size_ != 0 ? 1U : 0U);
    const std::uint64_t index_room = (static_cast<std::uint64_t>(file_size) - header_size) / 4U;
    if (block_count_ >= index_room) {
        open_error_ = "ZSO index extends past the end of the file";
        file_.close();
        return;
    }
}

bool ZsoReader::isOpen() const {
    return file_.is_open();
}

const std::string& ZsoReader::openError() const {
    return open_error_;
}

bool ZsoReader::readAt(std::uint64_t offset, std::span<std::byte> destination, std::string& error) {
    if (!file_) {
        error = open_error_.empty() ? "ZSO is not open" : open_error_;
        return false;
    }
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        error = "Requested ZSO offset is too large";
        return false;
    }

    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_) {
        error = "Could not seek in the ZSO";
        return false;
    }
    file_.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    if (file_.gcount() != static_cast<std::streamsize>(destination.size())) {
        error = "ZSO ended before the required bytes could be read";
        return false;
    }
    return true;
}

bool ZsoReader::ensureBlock(std::uint64_t block_index, std::string& error) {
    if (block_cached_ && cached_block_ == block_index) {
        return true;
    }
    if (block_index >= block_count_) {
        error = "ZSO ended before the required bytes could be read";
        return false;
    }

    std::array<std::byte, 8> index_bytes{};
    const std::uint64_t index_offset = static_cast<std::uint64_t>(kZsoHeaderSize) + block_index * 4U;
    if (!readAt(index_offset, index_bytes, error)) {
        return false;
    }

    const std::uint32_t first = loadLe32(index_bytes.data());
    const std::uint32_t second = loadLe32(index_bytes.data() + 4);
    const bool plain = (first & 0x80000000U) != 0;
    const std::uint64_t start = static_cast<std::uint64_t>(first & 0x7FFFFFFFU) << align_;
    const std::uint64_t end = static_cast<std::uint64_t>(second & 0x7FFFFFFFU) << align_;
    if (end < start) {
        error = "ZSO block index is out of order";
        return false;
    }

    const std::uint64_t stored = end - start;
    const std::uint64_t block_start = block_index * static_cast<std::uint64_t>(block_size_);
    if (block_start >= total_bytes_) {
        error = "ZSO ended before the required bytes could be read";
        return false;
    }
    const std::uint64_t logical = std::min<std::uint64_t>(block_size_, total_bytes_ - block_start);
    block_.resize(static_cast<std::size_t>(logical));

    if (plain) {
        if (stored < logical) {
            error = "ZSO uncompressed block is shorter than the ISO block";
            return false;
        }
        if (!readAt(start, std::span<std::byte>(block_.data(), block_.size()), error)) {
            return false;
        }
    } else {
        const std::uint64_t padding = std::uint64_t{1} << align_;
        const std::uint64_t maximum = logical + logical / 255U + 16U + padding;
        if (stored == 0 || stored > maximum) {
            error = "ZSO compressed block has an unsupported size";
            return false;
        }
        std::vector<std::byte> compressed(static_cast<std::size_t>(stored));
        if (!readAt(start, compressed, error)) {
            return false;
        }
        std::string lz4_error;
        if (!decompressLz4Block(compressed, block_, lz4_error)) {
            error = "Could not decompress the ZSO block: " + lz4_error;
            return false;
        }
    }

    cached_block_ = block_index;
    block_cached_ = true;
    return true;
}

bool ZsoReader::read(std::uint64_t offset, std::span<std::byte> destination, std::string& error) {
    if (!file_) {
        error = open_error_.empty() ? "ZSO is not open" : open_error_;
        return false;
    }
    if (offset > total_bytes_ || destination.size() > total_bytes_ - offset) {
        error = "ZSO ended before the required bytes could be read";
        return false;
    }

    std::size_t written = 0;
    while (written < destination.size()) {
        const std::uint64_t position = offset + written;
        const std::uint64_t block_index = position / block_size_;
        const std::uint64_t block_offset = position % block_size_;
        if (!ensureBlock(block_index, error)) {
            return false;
        }
        if (block_offset >= block_.size()) {
            error = "ZSO ended before the required bytes could be read";
            return false;
        }
        const std::size_t available = block_.size() - static_cast<std::size_t>(block_offset);
        const std::size_t take = std::min(available, destination.size() - written);
        std::copy_n(block_.begin() + static_cast<std::ptrdiff_t>(block_offset),
                    static_cast<std::ptrdiff_t>(take),
                    destination.begin() + static_cast<std::ptrdiff_t>(written));
        written += take;
    }
    return true;
}

} // namespace ps2br
