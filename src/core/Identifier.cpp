#include "core/Identifier.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ps2br {
namespace {

constexpr std::uint64_t kRootDirectorySectorOffset = 0x80A4;
constexpr std::uint64_t kRootDirectorySizeOffset = 0x80A6;
constexpr std::uint64_t kSectorSize = 2048;
constexpr std::size_t kSystemCnfExtentDistance = 31;
constexpr std::uint32_t kMaxRootDirectoryBytes = 64U * static_cast<std::uint32_t>(kSectorSize);

std::uint8_t byteValue(std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

char upperAscii(unsigned char value) {
    return static_cast<char>(std::toupper(value));
}

bool isUpperAlnum(char value) {
    const unsigned char byte = static_cast<unsigned char>(value);
    return std::isdigit(byte) || (value >= 'A' && value <= 'Z');
}

bool allOf(std::string_view text, std::size_t begin, std::size_t end, bool (*predicate)(char)) {
    for (std::size_t index = begin; index < end; ++index) {
        if (!predicate(text[index])) {
            return false;
        }
    }
    return true;
}

bool isLetter(char value) {
    return value >= 'A' && value <= 'Z';
}

bool isDigitChar(char value) {
    return value >= '0' && value <= '9';
}

std::string canonicalFromParts(std::string_view prefix, std::string_view five) {
    std::string id;
    id.reserve(11);
    id.append(prefix);
    id.push_back('_');
    id.append(five.substr(0, 3));
    id.push_back('.');
    id.append(five.substr(3, 2));
    return id;
}

// PlayStation executables use several spellings of the same serial. The
// database key stays AAAA_BBB.CC, except for on-disc product codes that are
// already 11 characters with the dot in that same place (LSP06010.10).
std::optional<std::string> normalizeToken(std::string_view token) {
    if (token.size() == 11 && token[8] == '.' && token[4] == '_' &&
        allOf(token, 0, 4, isUpperAlnum) && allOf(token, 5, 8, isUpperAlnum) &&
        allOf(token, 9, 11, isUpperAlnum)) {
        return std::string(token);
    }
    if (token.size() == 11 && token[8] == '.' && token[4] == '-' &&
        allOf(token, 0, 4, isUpperAlnum) && allOf(token, 5, 8, isUpperAlnum) &&
        allOf(token, 9, 11, isUpperAlnum)) {
        std::string id(token);
        id[4] = '_';
        return id;
    }
    if (token.size() == 11 && token[8] == '.' && isLetter(token[0]) &&
        allOf(token, 0, 8, isUpperAlnum) && allOf(token, 9, 11, isUpperAlnum)) {
        return std::string(token);
    }
    if (token.size() == 10 && token[7] == '.' && allOf(token, 0, 4, isLetter) &&
        allOf(token, 4, 7, isDigitChar) && allOf(token, 8, 10, isDigitChar)) {
        const std::string five{token[4], token[5], token[6], token[8], token[9]};
        return canonicalFromParts(token.substr(0, 4), five);
    }
    if (token.size() == 10 && (token[4] == '-' || token[4] == '_') &&
        allOf(token, 0, 4, isLetter) && allOf(token, 5, 10, isDigitChar)) {
        return canonicalFromParts(token.substr(0, 4), std::string_view(token.data() + 5, 5));
    }
    if (token.size() == 9 && allOf(token, 0, 4, isLetter) && allOf(token, 4, 9, isDigitChar)) {
        return canonicalFromParts(token.substr(0, 4), std::string_view(token.data() + 4, 5));
    }
    return std::nullopt;
}

std::string_view trimLeft(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    return text;
}

struct BootParse {
    bool found_line = false;
    bool playstation1 = true;
    std::optional<std::string> id;
};

BootParse parseBootLine(const std::string& text) {
    BootParse result;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t end = text.find_first_of("\r\n", cursor);
        const std::size_t line_end = end == std::string::npos ? text.size() : end;
        std::string_view line = trimLeft(std::string_view(text).substr(cursor, line_end - cursor));
        const bool boot2 = line.size() >= 5 && line.substr(0, 5) == "BOOT2" &&
                           (line.size() == 5 || line[5] == ' ' || line[5] == '\t' || line[5] == '=');
        const bool boot = !boot2 && line.size() >= 4 && line.substr(0, 4) == "BOOT" &&
                          (line.size() == 4 || line[4] == ' ' || line[4] == '\t' || line[4] == '=');
        if (boot || boot2) {
            result.found_line = true;
            const auto equals = line.find('=');
            if (equals == std::string_view::npos) {
                result.playstation1 = boot;
                return result;
            }
            std::string_view value = trimLeft(line.substr(equals + 1));
            const bool cdrom0 = value.find("CDROM0:") != std::string_view::npos;
            result.playstation1 = boot && !cdrom0;
            std::string_view path = value;
            if (cdrom0) {
                path = value.substr(value.find("CDROM0:") + 7);
            } else {
                const auto cdrom = value.find("CDROM:");
                if (cdrom != std::string_view::npos) {
                    path = value.substr(cdrom + 6);
                }
            }
            while (!path.empty() && (path.front() == ' ' || path.front() == '\\' || path.front() == '/')) {
                path.remove_prefix(1);
            }
            const auto token_end = path.find_first_of(" \t");
            std::string_view token = path.substr(0, token_end);
            const auto slash = token.find_last_of("\\/");
            if (slash != std::string_view::npos) {
                token.remove_prefix(slash + 1);
            }
            const auto semicolon = token.find(';');
            if (semicolon != std::string_view::npos) {
                token = token.substr(0, semicolon);
            }
            result.id = normalizeToken(token);
            return result;
        }
        if (end == std::string::npos) {
            break;
        }
        cursor = end + 1;
    }
    return result;
}

bool tokenStarts(std::string_view text, std::size_t index) {
    if (index == 0) {
        return true;
    }
    const char previous = text[index - 1];
    return !isUpperAlnum(previous) && previous != '_' && previous != '-';
}

bool tokenEnds(std::string_view text, std::size_t index) {
    if (index >= text.size()) {
        return true;
    }
    const char next = text[index];
    return !isUpperAlnum(next) && next != '_' && next != '-' && next != '.';
}

std::optional<std::string> firstSerialInText(std::string_view text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (!tokenStarts(text, index)) {
            continue;
        }
        for (const std::size_t length : {std::size_t{11}, std::size_t{10}, std::size_t{9}}) {
            if (index + length > text.size() || !tokenEnds(text, index + length)) {
                continue;
            }
            if (auto id = normalizeToken(text.substr(index, length))) {
                return id;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> serialsInDirectory(std::span<const std::byte> directory) {
    std::vector<std::string> ids;
    std::size_t offset = 0;
    while (offset + 33 < directory.size()) {
        const std::uint8_t record_length = byteValue(directory[offset]);
        if (record_length == 0) {
            break;
        }
        if (offset + record_length > directory.size()) {
            break;
        }
        const std::uint8_t name_length = byteValue(directory[offset + 32]);
        if (name_length > 0 && offset + 33 + name_length <= offset + record_length) {
            std::string name;
            name.reserve(name_length);
            for (std::uint8_t index = 0; index < name_length; ++index) {
                name.push_back(upperAscii(byteValue(directory[offset + 33 + index])));
            }
            const auto semicolon = name.find(';');
            if (semicolon != std::string::npos) {
                name.resize(semicolon);
            }
            if (name != "." && name != ".." && name != "SYSTEM.CNF") {
                if (auto id = normalizeToken(name)) {
                    if (std::find(ids.begin(), ids.end(), *id) == ids.end()) {
                        ids.push_back(std::move(*id));
                    }
                }
            }
        }
        offset += record_length;
    }
    return ids;
}

std::optional<std::string> chooseDirectorySerial(const std::vector<std::string>& ids, std::string_view text) {
    if (ids.size() == 1) {
        return ids.front();
    }
    std::vector<std::string> mentioned;
    for (const std::string& id : ids) {
        if (text.find(id) != std::string_view::npos) {
            mentioned.push_back(id);
        }
    }
    if (mentioned.size() == 1) {
        return mentioned.front();
    }
    return std::nullopt;
}

std::uint32_t readLe32(const std::array<std::byte, 4>& bytes) {
    return static_cast<std::uint32_t>(byteValue(bytes[0])) |
           (static_cast<std::uint32_t>(byteValue(bytes[1])) << 8U) |
           (static_cast<std::uint32_t>(byteValue(bytes[2])) << 16U) |
           (static_cast<std::uint32_t>(byteValue(bytes[3])) << 24U);
}

std::vector<std::byte> readDirectory(ByteReader& reader, std::uint64_t offset,
                                     const std::array<std::byte, kSectorSize>& first) {
    std::string ignored;
    std::array<std::byte, 4> directory_size{};
    std::uint32_t directory_bytes = static_cast<std::uint32_t>(kSectorSize);
    if (reader.read(kRootDirectorySizeOffset, directory_size, ignored)) {
        const std::uint32_t recorded = readLe32(directory_size);
        if (recorded > directory_bytes && recorded <= kMaxRootDirectoryBytes) {
            directory_bytes = recorded;
        }
    }
    std::vector<std::byte> directory(first.begin(), first.end());
    if (directory_bytes > directory.size()) {
        directory.resize(directory_bytes);
        if (!reader.read(offset, directory, ignored)) {
            directory.assign(first.begin(), first.end());
        }
    }
    return directory;
}

} // namespace

Identification identifyGame(ByteReader& reader) {
    Identification result;
    std::string error;

    std::array<std::byte, 2> root_location{};
    if (!reader.read(kRootDirectorySectorOffset, root_location, error)) {
        result.error = "Root-directory location: " + error;
        return result;
    }

    const std::uint32_t root_sector =
        (static_cast<std::uint32_t>(byteValue(root_location[0])) << 8U) |
        static_cast<std::uint32_t>(byteValue(root_location[1]));
    result.root_directory_offset = static_cast<std::uint64_t>(root_sector) * kSectorSize;

    std::array<std::byte, kSectorSize> root_directory{};
    if (!reader.read(result.root_directory_offset, root_directory, error)) {
        result.error = "Root directory: " + error;
        return result;
    }

    constexpr std::array<std::byte, 6> system_cnf_name{
        std::byte{0x45}, std::byte{0x4D}, std::byte{0x2E},
        std::byte{0x43}, std::byte{0x4E}, std::byte{0x46},
    };
    const auto cnf = std::search(root_directory.begin(), root_directory.end(),
                                 system_cnf_name.begin(), system_cnf_name.end());
    if (cnf == root_directory.end()) {
        const std::vector<std::byte> directory = readDirectory(reader, result.root_directory_offset, root_directory);
        if (auto id = chooseDirectorySerial(serialsInDirectory(directory), std::string_view{})) {
            result.game_id = std::move(*id);
            result.success = true;
            return result;
        }
        result.error = "SYSTEM.CNF directory record was not found";
        return result;
    }

    const std::size_t cnf_index = static_cast<std::size_t>(std::distance(root_directory.begin(), cnf));
    if (cnf_index < kSystemCnfExtentDistance) {
        result.error = "SYSTEM.CNF directory record is malformed";
        return result;
    }

    const std::size_t extent = cnf_index - kSystemCnfExtentDistance;
    const std::uint32_t system_sector =
        (static_cast<std::uint32_t>(byteValue(root_directory[extent])) << 24U) |
        (static_cast<std::uint32_t>(byteValue(root_directory[extent + 1])) << 16U) |
        (static_cast<std::uint32_t>(byteValue(root_directory[extent + 2])) << 8U) |
        static_cast<std::uint32_t>(byteValue(root_directory[extent + 3]));
    result.system_cnf_offset = static_cast<std::uint64_t>(system_sector) * kSectorSize;

    std::array<std::byte, kSectorSize> system_cnf{};
    std::size_t cnf_bytes = 0;
    if (reader.read(result.system_cnf_offset, system_cnf, error)) {
        cnf_bytes = system_cnf.size();
    } else {
        std::array<std::byte, 64> prefix{};
        if (!reader.read(result.system_cnf_offset, prefix, error)) {
            result.error = "SYSTEM.CNF: " + error;
            return result;
        }
        std::copy(prefix.begin(), prefix.end(), system_cnf.begin());
        cnf_bytes = prefix.size();
    }

    std::size_t text_bytes = cnf_bytes;
    for (std::size_t index = 0; index < cnf_bytes; ++index) {
        if (system_cnf[index] == std::byte{0}) {
            text_bytes = index;
            break;
        }
    }

    constexpr std::array<std::byte, 3> id_precursor{
        std::byte{0x30}, std::byte{0x3A}, std::byte{0x5C},
    };
    const auto text_end = system_cnf.begin() + static_cast<std::ptrdiff_t>(text_bytes);
    auto game_id_begin = std::search(system_cnf.begin(), text_end, id_precursor.begin(), id_precursor.end());
    if (game_id_begin != text_end) {
        // PlayStation 2 keeps the original 11-byte read. The bytes after
        // "0:\" are copied as stored, including their case.
        game_id_begin += static_cast<std::ptrdiff_t>(id_precursor.size());
        while (game_id_begin != system_cnf.end() && *game_id_begin == std::byte{0x5C}) {
            ++game_id_begin;
        }
        if (std::distance(game_id_begin, system_cnf.begin() + static_cast<std::ptrdiff_t>(cnf_bytes)) < 11) {
            result.error = "SYSTEM.CNF ended before the complete game ID";
            return result;
        }
        result.game_id.reserve(11);
        for (auto it = game_id_begin; it != game_id_begin + 11; ++it) {
            result.game_id.push_back(static_cast<char>(byteValue(*it)));
        }
        result.success = true;
        return result;
    }

    std::string text;
    text.reserve(text_bytes);
    for (std::size_t index = 0; index < text_bytes; ++index) {
        text.push_back(upperAscii(byteValue(system_cnf[index])));
    }

    const BootParse boot = parseBootLine(text);
    if (boot.id) {
        result.game_id = *boot.id;
        result.playstation1 = boot.playstation1;
        result.success = true;
        return result;
    }
    if (!boot.found_line) {
        if (auto id = firstSerialInText(text)) {
            result.game_id = std::move(*id);
            result.playstation1 = text.find("CDROM0:") == std::string::npos;
            result.success = true;
            return result;
        }
    }
    const std::vector<std::byte> directory =
        readDirectory(reader, result.root_directory_offset, root_directory);
    if (auto id = chooseDirectorySerial(serialsInDirectory(directory), text)) {
        result.game_id = std::move(*id);
        result.playstation1 = text.find("CDROM0:") == std::string::npos;
        result.success = true;
        return result;
    }

    result.error = "Game-ID precursor 0:\\ was not found in SYSTEM.CNF";
    return result;
}

} // namespace ps2br
