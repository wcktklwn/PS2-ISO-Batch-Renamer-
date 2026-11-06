#include "core/VcdReader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace ps2br {
namespace {

constexpr std::uint64_t kPopstarterHeaderSize = 0x100000;
constexpr std::uint32_t kCookedSectorSize = 2048;

std::uint8_t asByte(std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

} // namespace

VcdReader::VcdReader(const std::filesystem::path& path)
    : file_(path, std::ios::binary) {
    if (!file_) {
        open_error_ = "Could not open the VCD for reading";
        return;
    }

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        open_error_ = "Could not determine the VCD file size";
        file_.close();
        return;
    }

    struct Candidate {
        std::uint32_t raw_sector_size;
        std::uint32_t user_offset;
    };
    constexpr std::array<Candidate, 3> candidates{{
        {2352, 24},
        {2352, 16},
        {2048, 0},
    }};
    for (const Candidate& candidate : candidates) {
        const std::uint64_t minimum = kPopstarterHeaderSize +
                                      static_cast<std::uint64_t>(candidate.raw_sector_size) * 17U;
        if (static_cast<std::uint64_t>(file_size) < minimum) {
            continue;
        }
        if (!hasVolumeDescriptor(candidate.raw_sector_size, candidate.user_offset)) {
            continue;
        }
        raw_sector_size_ = candidate.raw_sector_size;
        user_offset_ = candidate.user_offset;
        const std::uint64_t payload = static_cast<std::uint64_t>(file_size) - kPopstarterHeaderSize;
        logical_bytes_ = (payload / raw_sector_size_) * kCookedSectorSize;
        return;
    }

    open_error_ = "VCD does not contain a recognizable disc image";
    file_.close();
}

bool VcdReader::isOpen() const {
    return file_.is_open();
}

const std::string& VcdReader::openError() const {
    return open_error_;
}

bool VcdReader::readAt(std::uint64_t offset, std::span<std::byte> destination, std::string& error) {
    if (!file_) {
        error = open_error_.empty() ? "VCD is not open" : open_error_;
        return false;
    }
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        error = "Requested VCD offset is too large";
        return false;
    }

    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_) {
        error = "Could not seek in the VCD";
        return false;
    }
    file_.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    if (file_.gcount() != static_cast<std::streamsize>(destination.size())) {
        error = "VCD ended before the required bytes could be read";
        return false;
    }
    return true;
}

bool VcdReader::hasVolumeDescriptor(std::uint32_t raw_sector_size, std::uint32_t user_offset) {
    std::array<std::byte, 6> descriptor{};
    const std::uint64_t offset = kPopstarterHeaderSize +
                                 static_cast<std::uint64_t>(raw_sector_size) * 16U + user_offset;
    std::string error;
    if (!readAt(offset, descriptor, error)) {
        return false;
    }
    return asByte(descriptor[0]) == 1 &&
           asByte(descriptor[1]) == 'C' && asByte(descriptor[2]) == 'D' &&
           asByte(descriptor[3]) == '0' && asByte(descriptor[4]) == '0' &&
           asByte(descriptor[5]) == '1';
}

bool VcdReader::read(std::uint64_t offset, std::span<std::byte> destination, std::string& error) {
    if (!file_) {
        error = open_error_.empty() ? "VCD is not open" : open_error_;
        return false;
    }
    if (raw_sector_size_ == 0 || offset > logical_bytes_ || destination.size() > logical_bytes_ - offset) {
        error = "VCD ended before the required bytes could be read";
        return false;
    }

    std::size_t written = 0;
    while (written < destination.size()) {
        const std::uint64_t position = offset + written;
        const std::uint64_t sector = position / kCookedSectorSize;
        const std::uint32_t intra = static_cast<std::uint32_t>(position % kCookedSectorSize);
        const std::uint32_t available = kCookedSectorSize - intra;
        const std::size_t take = std::min<std::size_t>(available, destination.size() - written);
        const std::uint64_t file_offset = kPopstarterHeaderSize + sector * raw_sector_size_ + user_offset_ + intra;
        if (!readAt(file_offset, destination.subspan(written, take), error)) {
            return false;
        }
        written += take;
    }
    return true;
}

} // namespace ps2br
