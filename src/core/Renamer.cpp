#include "core/Renamer.h"

#include "core/Identifier.h"
#include "core/IsoReader.h"
#include "core/MediaCatalog.h"
#include "core/PathText.h"
#include "core/VcdReader.h"
#include "core/ZsoReader.h"
#if PS2BR_ENABLE_CHD
#include "core/ChdReader.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

namespace ps2br {
namespace {

std::string lowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

// Case-insensitive order with digit runs compared by numeric value.
// Naturally equal names fall back to raw byte order so the relation stays strict.
int naturalCompare(std::string_view left, std::string_view right) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() && j < right.size()) {
        const unsigned char lc = static_cast<unsigned char>(left[i]);
        const unsigned char rc = static_cast<unsigned char>(right[j]);
        if (std::isdigit(lc) && std::isdigit(rc)) {
            while (i < left.size() && left[i] == '0') {
                ++i;
            }
            while (j < right.size() && right[j] == '0') {
                ++j;
            }
            const std::size_t left_digits = i;
            const std::size_t right_digits = j;
            while (i < left.size() && std::isdigit(static_cast<unsigned char>(left[i]))) {
                ++i;
            }
            while (j < right.size() && std::isdigit(static_cast<unsigned char>(right[j]))) {
                ++j;
            }
            const std::size_t left_length = i - left_digits;
            const std::size_t right_length = j - right_digits;
            if (left_length != right_length) {
                return left_length < right_length ? -1 : 1;
            }
            if (const int compared = left.compare(left_digits, left_length, right, right_digits, right_length);
                compared != 0) {
                return compared < 0 ? -1 : 1;
            }
            continue;
        }
        const char lfold = static_cast<char>(std::tolower(lc));
        const char rfold = static_cast<char>(std::tolower(rc));
        if (lfold != rfold) {
            return lfold < rfold ? -1 : 1;
        }
        ++i;
        ++j;
    }
    if (i != left.size() || j != right.size()) {
        return i == left.size() ? -1 : 1;
    }
    if (left == right) {
        return 0;
    }
    return left < right ? -1 : 1;
}

bool filenameLess(const std::filesystem::path& left, const std::filesystem::path& right) {
    const std::string left_name = pathToUtf8(left.filename());
    const std::string right_name = pathToUtf8(right.filename());
    if (const int compared = naturalCompare(left_name, right_name); compared != 0) {
        return compared < 0;
    }
    return pathToUtf8(left) < pathToUtf8(right);
}

bool isCancelled(const std::atomic_bool* cancel) {
    return cancel != nullptr && cancel->load();
}

const char* fileExtension(ImageType type) {
    switch (type) {
    case ImageType::Iso: return ".iso";
    case ImageType::Chd: return ".chd";
    case ImageType::Zso: return ".zso";
    case ImageType::Vcd: return ".vcd";
    case ImageType::Auto: return "";
    }
    return "";
}

std::optional<ImageType> imageTypeFromExtension(const std::filesystem::path& extension) {
    const std::string lower = lowerAscii(pathToUtf8(extension));
    if (lower == ".iso") return ImageType::Iso;
    if (lower == ".chd") return ImageType::Chd;
    if (lower == ".zso") return ImageType::Zso;
    if (lower == ".vcd") return ImageType::Vcd;
    return std::nullopt;
}

enum class LibraryFolder { None, Cd, Dvd, Pops };

LibraryFolder libraryFolderFromName(const std::filesystem::path& name) {
    const std::string lower = lowerAscii(pathToUtf8(name));
    if (lower == "cd") return LibraryFolder::Cd;
    if (lower == "dvd") return LibraryFolder::Dvd;
    if (lower == "pops") return LibraryFolder::Pops;
    return LibraryFolder::None;
}

const char* libraryFolderName(LibraryFolder folder) {
    switch (folder) {
    case LibraryFolder::Cd: return "CD";
    case LibraryFolder::Dvd: return "DVD";
    case LibraryFolder::Pops: return "POPS";
    case LibraryFolder::None: return "";
    }
    return "";
}

// An 80-minute CD holds 360,000 user-data sectors. Anything larger is a DVD.
constexpr std::uint64_t kCdUserDataLimit = 360000ULL * 2048ULL;

enum class MediaPlacement { Cd, Dvd, Pops, Stay };

MediaPlacement classifyMedia(const Identification& identification, const ByteReader& reader, ImageType type) {
    if (identification.playstation1) {
        return MediaPlacement::Pops;
    }
    if (const MediaKind* kind = MediaCatalog::builtIn().find(identification.game_id)) {
        switch (*kind) {
        case MediaKind::Cd: return MediaPlacement::Cd;
        case MediaKind::Dvd: return MediaPlacement::Dvd;
        case MediaKind::Playstation: return MediaPlacement::Pops;
        }
    }
    if (reader.hasCdLayout()) {
        return MediaPlacement::Cd;
    }
    if (type == ImageType::Chd || reader.logicalBytes() > kCdUserDataLimit) {
        return MediaPlacement::Dvd;
    }
    return MediaPlacement::Stay;
}

std::filesystem::path libraryRoot(const std::filesystem::path& folder) {
    if (libraryFolderFromName(folder.filename()) != LibraryFolder::None) {
        const std::filesystem::path parent = folder.parent_path();
        if (!parent.empty()) {
            return parent;
        }
    }
    return folder;
}

std::filesystem::path existingChildFolder(const std::filesystem::path& root, LibraryFolder folder) {
    const std::string wanted = lowerAscii(libraryFolderName(folder));
    std::error_code ec;
    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code directory_ec;
        if (it->is_directory(directory_ec) && lowerAscii(pathToUtf8(it->path().filename())) == wanted) {
            return it->path();
        }
    }
    return root / libraryFolderName(folder);
}

std::string destinationLabel(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (source.parent_path().lexically_normal() == destination.parent_path().lexically_normal()) {
        return pathToUtf8(destination.filename());
    }
    return pathToUtf8(destination.parent_path().filename() / destination.filename());
}

std::string filenameFor(const DatabaseRecord& record, NamingMode mode, ImageType type) {
    std::string filename = record.title;
    if (mode == NamingMode::TitleAndGameId) {
        filename += " (" + record.game_id + ")";
    }
    filename += fileExtension(type);
    return filename;
}

std::string portableFilenameError(std::string_view filename) {
    constexpr std::string_view forbidden = "<>:\"/\\|?*";
    for (unsigned char value : filename) {
        if (value < 32 || forbidden.find(static_cast<char>(value)) != std::string_view::npos) {
            return "Database title contains a character that is not valid in a Windows filename";
        }
    }
    if (filename.empty() || filename.back() == ' ' || filename.back() == '.') {
        return "Database title ends with a character that is not valid in a Windows filename";
    }

    const std::size_t dot = filename.find('.');
    const std::string stem = lowerAscii(std::string(filename.substr(0, dot)));
    constexpr std::array<std::string_view, 4> reserved{{"con", "prn", "aux", "nul"}};
    if (std::find(reserved.begin(), reserved.end(), stem) != reserved.end() ||
        (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
         stem[3] >= '1' && stem[3] <= '9')) {
        return "Database title produces a filename reserved by Windows";
    }
    return {};
}

template <typename Reader>
std::unique_ptr<ByteReader> openChecked(const std::filesystem::path& path, std::string& error) {
    auto reader = std::make_unique<Reader>(path);
    if (!reader->isOpen()) {
        error = reader->openError();
        return nullptr;
    }
    return reader;
}

std::unique_ptr<ByteReader> openReader(const std::filesystem::path& path, ImageType type, std::string& error) {
    switch (type) {
    case ImageType::Iso:
        return openChecked<IsoReader>(path, error);
    case ImageType::Zso:
        return openChecked<ZsoReader>(path, error);
    case ImageType::Vcd:
        return openChecked<VcdReader>(path, error);
    case ImageType::Chd:
#if PS2BR_ENABLE_CHD
        return openChecked<ChdReader>(path, error);
#else
        error = "This build does not include CHD support";
        return nullptr;
#endif
    case ImageType::Auto:
        break;
    }
    error = "Unsupported image type";
    return nullptr;
}

} // namespace

const char* toString(ImageType type) {
    switch (type) {
    case ImageType::Iso: return "ISO";
    case ImageType::Chd: return "CHD";
    case ImageType::Zso: return "ZSO";
    case ImageType::Vcd: return "VCD";
    case ImageType::Auto: return "Auto";
    }
    return "Unknown";
}

const char* toString(ResultStatus status) {
    switch (status) {
    case ResultStatus::Pending: return "Pending";
    case ResultStatus::Scanning: return "Scanning";
    case ResultStatus::Ready: return "Ready";
    case ResultStatus::Renamed: return "Renamed";
    case ResultStatus::Skipped: return "Skipped";
    case ResultStatus::Failed: return "Failed";
    }
    return "Unknown";
}

Renamer::Renamer(const GameDatabase& database) : database_(database) {}

std::vector<RenameResult> Renamer::scan(
    const ScanOptions& options,
    const ActivityCallback& activity,
    const ResultCallback& result_update,
    const std::atomic_bool* cancel) const {

    struct ImageFile {
        std::filesystem::path path;
        ImageType type = ImageType::Iso;
        std::size_t folder_index = 0;
    };
    std::vector<ImageFile> paths;
    std::vector<RenameResult> enumeration_failures;

    const auto appendDirectory = [&](const std::filesystem::path& directory) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec)) {
                continue;
            }
            const std::optional<ImageType> type = imageTypeFromExtension(it->path().extension());
            if (!type || (options.image_type != ImageType::Auto && *type != options.image_type)) {
                continue;
            }
            paths.push_back({it->path(), *type, paths.size()});
        }
        if (ec) {
            RenameResult failure;
            failure.source_path = directory;
            failure.image_type = options.image_type;
            failure.status = ResultStatus::Failed;
            failure.detail = "Could not enumerate " + pathToUtf8(directory) + ": " + ec.message();
            enumeration_failures.push_back(std::move(failure));
        }
    };

    appendDirectory(options.folder);
    if (!enumeration_failures.empty() && paths.empty() && enumeration_failures.front().source_path == options.folder) {
        if (activity) activity("FAILED: " + enumeration_failures.front().detail);
        if (result_update) result_update(0, 1, enumeration_failures.front());
        return {enumeration_failures.front()};
    }
    if (libraryFolderFromName(options.folder.filename()) == LibraryFolder::None) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(options.folder, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code directory_ec;
            if (it->is_directory(directory_ec) &&
                libraryFolderFromName(it->path().filename()) != LibraryFolder::None) {
                appendDirectory(it->path());
            }
        }
        if (ec) {
            RenameResult failure;
            failure.source_path = options.folder;
            failure.image_type = options.image_type;
            failure.status = ResultStatus::Failed;
            failure.detail = "Could not enumerate " + pathToUtf8(options.folder) + ": " + ec.message();
            enumeration_failures.push_back(std::move(failure));
        }
    }

    if (options.sort_files) {
        std::sort(paths.begin(), paths.end(), [](const ImageFile& left, const ImageFile& right) {
            return filenameLess(left.path, right.path);
        });
    }

    std::vector<RenameResult> results(paths.size());
    if (activity) {
        if (options.image_type == ImageType::Auto) {
            activity("Found " + std::to_string(paths.size()) + " compatible images.");
        } else {
            activity("Found " + std::to_string(paths.size()) + " " + toString(options.image_type) + " files.");
        }
    }

    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (isCancelled(cancel)) {
            if (activity) activity("Scan cancelled.");
            results.resize(index);
            break;
        }

        auto& item = results[index];
        item.source_path = paths[index].path;
        item.image_type = paths[index].type;
        item.folder_index = paths[index].folder_index;
        item.status = ResultStatus::Scanning;
        item.detail = "Reading disc metadata";
        if (result_update) result_update(index, results.size(), item);
        if (activity) {
            activity("Scanning " + std::to_string(index + 1) + "/" + std::to_string(paths.size()) + ": " + pathToUtf8(paths[index].path.filename()));
        }

        std::string open_error;
        auto reader = openReader(paths[index].path, paths[index].type, open_error);
        if (!reader) {
            item.status = ResultStatus::Failed;
            item.detail = open_error;
            if (activity) activity("FAILED: " + pathToUtf8(paths[index].path.filename()) + " - " + item.detail);
            if (result_update) result_update(index, results.size(), item);
            continue;
        }

        const Identification identification = identifyGame(*reader);
        if (!identification.success) {
            item.status = ResultStatus::Failed;
            item.detail = identification.error;
            if (activity) activity("FAILED: " + pathToUtf8(paths[index].path.filename()) + " - " + item.detail);
            if (result_update) result_update(index, results.size(), item);
            continue;
        }
        item.game_id = identification.game_id;

        const DatabaseRecord* record = database_.findFirst(item.game_id);
        if (record == nullptr) {
            item.status = ResultStatus::Failed;
            item.detail = "Game ID " + item.game_id + " is not present in the database";
            if (activity) activity("FAILED: " + pathToUtf8(paths[index].path.filename()) + " - " + item.detail);
            if (result_update) result_update(index, results.size(), item);
            continue;
        }

        item.database_title = record->title;
        const std::string destination_filename = filenameFor(*record, options.naming_mode, paths[index].type);
        if (const std::string filename_error = portableFilenameError(destination_filename); !filename_error.empty()) {
            item.status = ResultStatus::Failed;
            item.detail = filename_error;
            if (activity) activity("FAILED: " + pathToUtf8(paths[index].path.filename()) + " - " + item.detail);
            if (result_update) result_update(index, results.size(), item);
            continue;
        }

        std::filesystem::path destination_directory = item.source_path.parent_path();
        std::string placement_detail;
        if (options.image_type == ImageType::Auto) {
            switch (classifyMedia(identification, *reader, paths[index].type)) {
            case MediaPlacement::Cd:
                destination_directory = existingChildFolder(libraryRoot(options.folder), LibraryFolder::Cd);
                placement_detail = "Move to CD";
                break;
            case MediaPlacement::Dvd:
                destination_directory = existingChildFolder(libraryRoot(options.folder), LibraryFolder::Dvd);
                placement_detail = "Move to DVD";
                break;
            case MediaPlacement::Pops:
                destination_directory = existingChildFolder(libraryRoot(options.folder), LibraryFolder::Pops);
                placement_detail = "Move to POPS";
                break;
            case MediaPlacement::Stay:
                placement_detail = "Media type is unknown, so the file stays in the current folder.";
                break;
            }
        }
        item.destination_path = destination_directory / pathFromUtf8(destination_filename);
        if (item.source_path.lexically_normal() == item.destination_path.lexically_normal()) {
            item.status = ResultStatus::Skipped;
            item.detail = placement_detail.find("unknown") == std::string::npos
                ? "Already has the requested filename"
                : "Already has the requested filename. " + placement_detail;
            if (activity) activity("Skipped: " + pathToUtf8(item.source_path.filename()) + " is already named correctly.");
        } else {
            item.status = ResultStatus::Ready;
            item.detail = placement_detail.empty() ? "Ready to rename" : placement_detail;
            if (activity) {
                activity("Ready: " + pathToUtf8(item.source_path.filename()) + " -> " +
                         destinationLabel(item.source_path, item.destination_path));
            }
        }
        if (result_update) result_update(index, results.size(), item);
    }

    if (!enumeration_failures.empty()) {
        const std::size_t offset = results.size();
        for (std::size_t index = 0; index < enumeration_failures.size(); ++index) {
            enumeration_failures[index].folder_index = paths.size() + index;
        }
        results.insert(results.end(), enumeration_failures.begin(), enumeration_failures.end());
        for (std::size_t index = 0; index < enumeration_failures.size(); ++index) {
            if (activity) activity("FAILED: " + results[offset + index].detail);
            if (result_update) result_update(offset + index, results.size(), results[offset + index]);
        }
    }

    return results;
}

RunSummary Renamer::apply(
    std::vector<RenameResult>& results,
    const ActivityCallback& activity,
    const ResultCallback& result_update,
    const std::atomic_bool* cancel) const {

    for (std::size_t index = 0; index < results.size(); ++index) {
        auto& item = results[index];
        if (isCancelled(cancel)) {
            if (activity) activity("Rename operation cancelled.");
            break;
        }
        if (item.status != ResultStatus::Ready) {
            continue;
        }

        if (activity) {
            activity("Renaming " + std::to_string(index + 1) + "/" + std::to_string(results.size()) + ": " +
                     pathToUtf8(item.source_path.filename()));
        }

        std::error_code ec;
        const std::filesystem::path parent = item.destination_path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                item.status = ResultStatus::Failed;
                item.detail = "Could not create the destination folder: " + ec.message();
                if (activity) {
                    activity("FAILED: " + pathToUtf8(item.source_path.filename()) + " - " + item.detail);
                }
                if (result_update) result_update(index, results.size(), item);
                continue;
            }
        }
        ec.clear();
        if (std::filesystem::exists(item.destination_path, ec)) {
            item.status = ResultStatus::Failed;
            item.detail = "Destination already exists; nothing was overwritten";
        } else if (ec) {
            item.status = ResultStatus::Failed;
            item.detail = "Could not check the destination: " + ec.message();
        } else {
            std::filesystem::rename(item.source_path, item.destination_path, ec);
            if (ec) {
                item.status = ResultStatus::Failed;
                item.detail = "Rename failed: " + ec.message();
            } else {
                item.status = ResultStatus::Renamed;
                item.detail = "Renamed successfully";
            }
        }

        if (activity) {
            const std::string prefix = item.status == ResultStatus::Renamed ? "Renamed: " : "FAILED: ";
            activity(prefix + pathToUtf8(item.source_path.filename()) +
                     (item.status == ResultStatus::Renamed
                          ? " -> " + destinationLabel(item.source_path, item.destination_path)
                          : " - " + item.detail));
        }
        if (result_update) result_update(index, results.size(), item);
    }

    return summarize(results);
}

void sortResultsByFilename(std::vector<RenameResult>& results) {
    std::sort(results.begin(), results.end(), [](const RenameResult& left, const RenameResult& right) {
        return filenameLess(left.source_path, right.source_path);
    });
}

void sortResultsByFolderOrder(std::vector<RenameResult>& results) {
    std::stable_sort(results.begin(), results.end(), [](const RenameResult& left, const RenameResult& right) {
        return left.folder_index < right.folder_index;
    });
}

RunSummary Renamer::summarize(const std::vector<RenameResult>& results) {
    RunSummary summary;
    summary.total = results.size();
    for (const auto& item : results) {
        switch (item.status) {
        case ResultStatus::Ready: ++summary.ready; break;
        case ResultStatus::Renamed: ++summary.renamed; break;
        case ResultStatus::Skipped: ++summary.skipped; break;
        case ResultStatus::Failed: ++summary.failed; break;
        default: break;
        }
    }
    return summary;
}

} // namespace ps2br
