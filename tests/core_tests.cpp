#include "core/Database.h"
#include "core/Identifier.h"
#include "core/IsoReader.h"
#include "core/MediaCatalog.h"
#include "core/OnlineDatabase.h"
#include "core/Renamer.h"
#include "core/VcdReader.h"
#include "core/ZsoReader.h"
#if PS2BR_ENABLE_CHD
#include "core/ChdReader.h"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#ifndef PS2BR_EXAMPLES_DIR
#define PS2BR_EXAMPLES_DIR ""
#endif

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void createFixture(const fs::path& path, std::string_view game_id, bool include_cnf = true) {
    constexpr std::uint32_t root_sector = 0x0105;
    constexpr std::uint32_t cnf_sector = 0x0180;
    constexpr std::size_t sector_size = 2048;
    constexpr std::size_t cnf_name_index = 0x482;
    std::vector<unsigned char> image((cnf_sector + 1) * sector_size, 0);

    image[0x8000] = 1;
    constexpr std::array<unsigned char, 5> iso_signature{'C', 'D', '0', '0', '1'};
    std::copy(iso_signature.begin(), iso_signature.end(), image.begin() + 0x8001);
    image[0x80A4] = static_cast<unsigned char>((root_sector >> 8U) & 0xFFU);
    image[0x80A5] = static_cast<unsigned char>(root_sector & 0xFFU);

    if (include_cnf) {
        const std::size_t root = root_sector * sector_size;
        const std::size_t extent = root + cnf_name_index - 31;
        image[extent] = static_cast<unsigned char>((cnf_sector >> 24U) & 0xFFU);
        image[extent + 1] = static_cast<unsigned char>((cnf_sector >> 16U) & 0xFFU);
        image[extent + 2] = static_cast<unsigned char>((cnf_sector >> 8U) & 0xFFU);
        image[extent + 3] = static_cast<unsigned char>(cnf_sector & 0xFFU);
        constexpr std::array<unsigned char, 6> name{'E', 'M', '.', 'C', 'N', 'F'};
        std::copy(name.begin(), name.end(), image.begin() + static_cast<std::ptrdiff_t>(root + cnf_name_index));

        const std::string system_cnf = "BOOT2 = cdrom0:\\" + std::string(game_id) + ";1\r\nVER = 1.00\r\n";
        std::copy(system_cnf.begin(), system_cnf.end(), image.begin() + static_cast<std::ptrdiff_t>(cnf_sector * sector_size));
    }

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
}

void databaseTests() {
    ps2br::GameDatabase database;
    std::string error;
    expect(database.loadText(
        "SLUS_217.20 First title\r\n"
        "SLUS_217.20 Second title\r\n"
        "SLES_518.26 AFL Live 2004\r\n"
        "SLES_510.17 Scooby-Doo^! and The Night of 100 Frights  \r\n",
        "test", error), "database should load: " + error);
    expect(database.recordCount() == 4, "database should retain every record");
    expect(database.uniqueIdCount() == 3, "database should count unique IDs");
    expect(database.duplicateIdCount() == 1, "database should report duplicate IDs");
    const auto* record = database.findFirst("SLUS_217.20");
    expect(record != nullptr && record->title == "First title", "lookup must preserve first-match-wins behaviour");
    const auto* legacy_title = database.findFirst("SLES_510.17");
    expect(legacy_title != nullptr &&
               legacy_title->title == "Scooby-Doo! and The Night of 100 Frights",
           "legacy ^! escaping and trailing database padding must not appear in filenames");

    ps2br::GameDatabase alphanumeric;
    expect(alphanumeric.loadText("KOEI_SP0.02 Koei Spring Pack\r\n", "test", error),
           "valid alphanumeric product IDs should load: " + error);
    ps2br::GameDatabase malformed;
    expect(!malformed.loadText("SLES-501.78 Oni\r\n", "test", error),
           "a database record with a hyphen in place of the canonical underscore must fail");
    expect(error.find("line 1") != std::string::npos,
           "a malformed database record should report its physical line number");

    ps2br::GameDatabase built_in;
    expect(built_in.loadBuiltIn(error), "built-in database should load: " + error);
    expect(built_in.recordCount() == 24477, "built-in database should contain the current record count");
    expect(built_in.uniqueIdCount() == 24477, "every built-in database ID should be unique");
    expect(built_in.duplicateIdCount() == 0, "built-in database should contain no duplicate IDs");
    constexpr std::array<std::pair<std::string_view, std::string_view>, 62> audited_titles{{
        {"PCPX_966.49", "Gran Turismo 4 - First Preview"},
        {"SCAJ_200.12", "Venus & Braves"},
        {"SCKA_200.58", "Action Romance Bumpy Trot"},
        {"SCKA_200.62", "Saru! Get You! 3"},
        {"SLAJ_250.23", "Shin Sangoku Musou 3 - Moushouden"},
        {"SLES_503.82", "Silent Hill 2"},
        {"SLES_512.27", "Lara Croft Tomb Raider - The Angel of Darkness"},
        {"SLES_512.52", "The Lord of the Rings - The Two Towers"},
        {"SLES_512.58", "007 - Nightfire [Platinum]"},
        {"SLES_518.97", "The Simpsons - Hit & Run"},
        {"SLES_520.05", "007 - Everything or Nothing"},
        {"SLES_525.88", "Mercenaries - Playground of Destruction"},
        {"SLES_525.90", "Mercenaries - Playground of Destruction"},
        {"SLES_528.01", "The Lord of the Rings - The Third Age"},
        {"SLES_529.74", "GoldenEye - Rogue Agent"},
        {"SLES_533.42", "Cricket 2005"},
        {"SLES_533.57", "21 Card Games"},
        {"SLES_533.66", "Killer7"},
        {"SLES_540.16", "AND 1 Streetball"},
        {"SLES_541.47", "Ice Age 2 - The Meltdown"},
        {"SLES_542.09", "The Sopranos - Road to Respect"},
        {"SLES_543.47", "The Sims 2 - Pets"},
        {"SLES_548.15", "The Legend of Spyro - The Eternal Night"},
        {"SLES_549.98", "Mercenaries 2 - L'enfer des Favelas"},
        {"SLES_820.18", "Cy Girls (Ice Disc)"},
        {"SLES_820.20", "Cy Girls (Ice Disc)"},
        {"SLES_820.21", "Cy Girls (Aska Disc)"},
        {"SLKA_251.03", "Shinseiki Evangelion 2 - Evangelions"},
        {"SLKA_251.31", "Jyuouki - Project Altered Beast"},
        {"SLKA_253.17", "Jin Samguk Mussang 3"},
        {"SLPM_650.40", "The Fear [Disc 1]"},
        {"SLPS_251.95", "Venus & Braves - Majo to Megami to Horobi no Yogen [Premium Box]"},
        {"SLPS_251.96", "Venus & Braves - Majo to Megami to Horobi no Yogen"},
        {"SLPS_254.85", "Kidou Senshi Gundam - Ver. 1.5 [Gundam the Best]"},
        {"SLPS_732.36", "Venus & Braves - Majo to Megami to Horobi no Yogen [PlayStation 2 The Best]"},
        {"SLES_546.04", "\xC2\xA1Qu\xC3\xA9 Pasa Neng! El Videojuego"},
        {"SLPM_550.11", "Poi Hito Natsu no Keiken! [Limited Edition]"},
        {"SLPM_550.12", "Ppoi! Hito Natsu no Keiken!"},
        {"SLPM_551.32", "Kira Kira - Rock 'N' Roll Show [First Print Limited Edition]"},
        {"SLPM_551.33", "Kira Kira - Rock 'n' Roll Show"},
        {"SLPM_551.49", "Loveroot Zero - Kiss Kiss Labyrinth"},
        {"SLPM_552.79", "Otometeki Koi Kakumei - Love Revo!! [Best Edition]"},
        {"SLPM_552.88", "Shin Koihime Musou - Otome Ryouran Sangokushi Engi"},
        {"SLPM_620.93", "Simple 2000 Series Ultimate Vol. 1 - Love Smash!"},
        {"SLPS_200.61", "Mamimume Mogacho no Print Hour"},
        {"SLPS_258.83", "Katekyoo Hitman Reborn! Nerae! Ring x Vongola Trainers"},
        {"SLPS_258.99", "Katekyoo Hitman Reborn! Let's Ansatsu! Nerawareta 10-daime! [Best Collection]"},
        {"SLPS_259.09", "Hisshou Pachinko Pachi-Slot Kouryaku Series Vol. 13 - Shin Seiki Evangelion - Yakusoku no Toki"},
        {"SLPS_259.11", "Hisshou Pachinko Pachi-Slot Kouryaku Series Vol. 11 - Shin Seiki Evangelion - Magokoro o, Kimi ni"},
        {"SLPS_259.31", "Katekyoo Hitman Reborn! Nerae! Ring x Vongola Trainers [Best Collection]"},
        {"SLPS_259.42", "Hisshou Pachinko Pachi-Slot Kouryaku Series Vol. 14 - CR Shin Seiki Evangelion - Saigo no Shisha"},
        {"SLPS_259.43", "Hisshou Pachinko Pachi-Slot Kouryaku Series Vol. 14 - CR Shin Seiki Evangelion - Saigo no Shisha [Limited Special Box]"},
        {"SLES_511.68", "AFL Live 2003"},
        {"SLES_518.26", "AFL Live 2004"},
        {"SLES_519.03", "AFL Live 2004 - Aussie Rules Football"},
        {"SLES_504.87", "Looney Tunes - Space Race"},
        {"SLPM_620.89", "Wacky Races Starring Dastardly & Muttley [Cancelled Japanese Release]"},
        {"SLPM_650.49", "Beatmania II DX 5th Style - New Songs Collection"},
        {"SLPM_650.52", "Gitadora! GuitarFreaks 4th Mix & DrumMania 3rd Mix"},
        {"SLKA_253.23", "FIFA 06"},
        {"SLPM_602.65", "Steamboy [Trial]"},
        {"SCES_609.71", "Kingdom Hearts II - Final Mix + [English Translation]"},
    }};
    for (const auto& [game_id, expected_title] : audited_titles) {
        const auto* audited = built_in.findFirst(game_id);
        expect(audited != nullptr && audited->title == expected_title,
               "built-in database should contain the audited title for " + std::string(game_id));
    }
    constexpr std::array<std::string_view, 36> unofficial_ids{
        "USGR_000.00", "USGR_000.01", "USGR_000.02", "USGR_000.03", "USGR_000.04", "USGR_000.05",
        "USGR_000.06", "USGR_000.07", "USGR_000.08", "USGR_000.09", "USGR_000.10", "USGR_000.11",
        "USGR_000.12", "USGR_000.13", "USGR_000.15", "USGR_000.16", "USGR_000.18", "USGR_000.19",
        "USGR_000.20", "USGR_000.21", "USGR_000.22", "USGR_000.23", "USGR_000.24", "USGR_000.25",
        "USGR_000.26", "USGR_000.34", "USGR_000.35", "USGR_000.36", "USGR_000.37", "USGR_000.38",
        "USGR_000.39", "USGR_000.40", "USGR_000.41", "USGR_999.97", "USGR_999.98", "USGR_999.99",
    };
    for (const auto id : unofficial_ids) {
        expect(built_in.findFirst(id) != nullptr, "built-in database should include documented unofficial ID " +
                                                     std::string(id));
    }
    const auto* taito = built_in.findFirst("USGR_000.25");
    expect(taito != nullptr && taito->title == "Taito Legends 1 & 2",
           "USGR_000.25 should resolve to the unofficial Taito Legends 1 & 2 compilation");
    const auto* gex = built_in.findFirst("SLUS_005.98");
    expect(gex != nullptr && gex->title == "Gex - Enter the Gecko",
           "POPStarter example serial SLUS_005.98 should resolve to Gex - Enter the Gecko");
    const auto* jet_moto = built_in.findFirst("SCUS_945.55");
    expect(jet_moto != nullptr && jet_moto->title == "Jet Moto 3",
           "POPStarter example serial SCUS_945.55 should resolve to Jet Moto 3");
    const auto* final_fantasy_vii = built_in.findFirst("SCUS_941.63");
    expect(final_fantasy_vii != nullptr && final_fantasy_vii->title == "Final Fantasy VII (USA)",
           "Redump PlayStation serial SCUS_941.63 should resolve to Final Fantasy VII (USA)");
    const auto* klonoa = built_in.findFirst("SCES_009.42");
    expect(klonoa != nullptr && klonoa->title == "Klonoa - Door to Phantomile (Europe)",
           "SCES_009.42 should use the Redump PlayStation title; Secret Agent Clank keeps SCES_554.96");
    const auto& media = ps2br::MediaCatalog::builtIn();
    expect(media.size() == 23093, "built-in media catalog should contain the Redump serial set");
    const ps2br::MediaKind* arcana_media = media.find("SLUS_217.20");
    const ps2br::MediaKind* ico_media = media.find("SCUS_971.13");
    const ps2br::MediaKind* gex_media = media.find("SLUS_005.98");
    expect(arcana_media != nullptr && *arcana_media == ps2br::MediaKind::Dvd,
           "SLUS_217.20 should be a Redump PlayStation 2 DVD serial");
    expect(ico_media != nullptr && *ico_media == ps2br::MediaKind::Cd,
           "SCUS_971.13 should be a Redump PlayStation 2 CD serial");
    expect(gex_media != nullptr && *gex_media == ps2br::MediaKind::Playstation,
           "SLUS_005.98 should be a Redump PlayStation serial");
    expect(media.find("SLUS_999.99") == nullptr, "an unused serial should be absent from the media catalog");
    const auto* retail_kingdom_hearts = built_in.findFirst("SLPM_666.75");
    expect(retail_kingdom_hearts != nullptr &&
               retail_kingdom_hearts->title == "Kingdom Hearts II - Final Mix +",
           "the retail ID must not inherit the unofficial English-translation suffix");
}

std::string onlineFixture(std::size_t records, bool duplicate_last = false) {
    std::string text;
    text.reserve(records * 32);
    for (std::size_t index = 0; index < records; ++index) {
        const std::size_t serial = duplicate_last && index + 1 == records ? 0 : index;
        char game_id[32]{};
        std::snprintf(game_id, sizeof(game_id), "TST0_%03zu.%02zu", serial / 100, serial % 100);
        text += game_id;
        text += " Validation Fixture ";
        text += std::to_string(index);
        text += '\n';
    }
    return text;
}

void onlineDatabaseTests() {
    std::string error;
    const std::string complete = onlineFixture(ps2br::kOnlineDatabaseMinimumRecords);
    expect(ps2br::validateOnlineDatabase(complete, error),
           "a complete unique online database should validate: " + error);

    const std::string incomplete = onlineFixture(ps2br::kOnlineDatabaseMinimumRecords - 1);
    expect(!ps2br::validateOnlineDatabase(incomplete, error) &&
               error.find("incomplete") != std::string::npos,
           "an incomplete download must be rejected");

    const std::string duplicate = onlineFixture(ps2br::kOnlineDatabaseMinimumRecords, true);
    expect(!ps2br::validateOnlineDatabase(duplicate, error) &&
               error.find("duplicate") != std::string::npos,
           "a downloaded database with duplicate IDs must be rejected");

    std::string malformed = complete;
    malformed.replace(0, 11, "TST0-000.00");
    expect(!ps2br::validateOnlineDatabase(malformed, error) &&
               error.find("line 1") != std::string::npos,
           "a malformed downloaded ID must be rejected with a line number");

    const std::string oversized(ps2br::kOnlineDatabaseMaximumBytes + 1, 'x');
    expect(!ps2br::validateOnlineDatabase(oversized, error) &&
               error.find("4 MiB") != std::string::npos,
           "an oversized download must be rejected before parsing");

    ps2br::GameDatabase active;
    expect(active.loadText("KEEP_000.01 Offline Fallback\n", "fallback", error),
           "fallback fixture should load");
    if (ps2br::validateOnlineDatabase(incomplete, error)) {
        active.loadText(incomplete, "unexpected", error);
    }
    const auto* fallback = active.findFirst("KEEP_000.01");
    expect(fallback != nullptr && fallback->title == "Offline Fallback",
           "a rejected online database must leave the active fallback unchanged");

#if !PS2BR_ENABLE_ONLINE_DATABASE
    const auto disabled = ps2br::downloadOnlineDatabase();
    expect(!disabled.success && disabled.error.find("disabled") != std::string::npos,
           "offline builds should return the explicit disabled result without networking");
#endif
}

void identifierTests(const fs::path& directory) {
    const fs::path valid = directory / "unknown.iso";
    createFixture(valid, "SLUS_217.20");
    ps2br::IsoReader reader(valid);
    const auto identification = ps2br::identifyGame(reader);
    expect(identification.success, "valid fixture should identify");
    expect(identification.game_id == "SLUS_217.20", "identifier should return the exact 11-byte ID");
    expect(!identification.playstation1, "a cdrom0: serial should stay on the PlayStation 2 path");
    expect(identification.root_directory_offset == 0x82800, "root-directory offset should match the V2/V3 algorithm");
    expect(identification.system_cnf_offset == 0xC0000, "SYSTEM.CNF offset should match the V2/V3 algorithm");

    const fs::path invalid = directory / "invalid.iso";
    createFixture(invalid, "SLUS_217.20", false);
    ps2br::IsoReader bad_reader(invalid);
    const auto bad = ps2br::identifyGame(bad_reader);
    expect(!bad.success && bad.error.find("not found") != std::string::npos,
           "missing SYSTEM.CNF should produce a visible failure");
}

void renamerTests(const fs::path& directory) {
    ps2br::GameDatabase database;
    std::string error;
    expect(database.loadText("SLUS_217.20 Arcana Heart\r\n", "test", error), "test database should load");

    ps2br::Renamer renamer(database);
    ps2br::ScanOptions options{directory, ps2br::ImageType::Iso, ps2br::NamingMode::TitleAndGameId};
    auto results = renamer.scan(options, {}, {});
    expect(results.size() == 2, "scan should include both ISO fixtures");

    auto found = std::find_if(results.begin(), results.end(), [](const auto& item) {
        return item.game_id == "SLUS_217.20";
    });
    expect(found != results.end(), "scan should retain detected game ID");
    if (found != results.end()) {
        expect(found->destination_path.filename() == "Arcana Heart (SLUS_217.20).iso",
               "GameName naming mode should append the ID in parentheses");
    }

    const auto summary = renamer.apply(results, {}, {});
    expect(summary.renamed == 1, "one valid fixture should be renamed");
    expect(summary.failed == 1, "one invalid fixture should remain visibly failed");
    expect(fs::exists(directory / "Arcana Heart (SLUS_217.20).iso"), "renamed file should exist");
    expect(!fs::exists(directory / "failed.txt"), "results must stay in memory unless the user saves a report");
    expect(!fs::exists(directory / "PS2_iso_files.txt"), "scanning must not leave a generated file list behind");
}

void safetyTests(const fs::path& root) {
    std::string error;

    const fs::path invalid_title_directory = root / "invalid-title";
    fs::create_directories(invalid_title_directory);
    createFixture(invalid_title_directory / "game.iso", "SLUS_217.20");
    ps2br::GameDatabase invalid_title_database;
    expect(invalid_title_database.loadText("SLUS_217.20 Broken * Title\r\n", "test", error),
           "database with a legacy-invalid title should still load");
    ps2br::Renamer invalid_title_renamer(invalid_title_database);
    auto invalid_title_results = invalid_title_renamer.scan(
        {invalid_title_directory, ps2br::ImageType::Iso, ps2br::NamingMode::TitleOnly}, {}, {});
    expect(invalid_title_results.size() == 1 && invalid_title_results[0].status == ps2br::ResultStatus::Failed,
           "a title invalid on Windows should be shown as failed during preview on every platform");
    expect(fs::exists(invalid_title_directory / "game.iso"), "an invalid database title must not rename the source");

    const fs::path collision_directory = root / "collision";
    fs::create_directories(collision_directory);
    createFixture(collision_directory / "game.iso", "SLUS_217.20");
    createFixture(collision_directory / "Arcana Heart.iso", "SLUS_217.20");
    ps2br::GameDatabase collision_database;
    expect(collision_database.loadText("SLUS_217.20 Arcana Heart\r\n", "test", error),
           "collision test database should load");
    ps2br::Renamer collision_renamer(collision_database);
    auto collision_results = collision_renamer.scan(
        {collision_directory, ps2br::ImageType::Iso, ps2br::NamingMode::TitleOnly}, {}, {});
    const auto collision_summary = collision_renamer.apply(collision_results, {}, {});
    expect(collision_summary.failed == 1, "an existing destination should produce a visible failure");
    expect(fs::exists(collision_directory / "game.iso"), "the source must remain when a destination exists");
}

void putLe32(std::vector<unsigned char>& output, std::uint32_t value) {
    output.push_back(static_cast<unsigned char>(value & 0xFFU));
    output.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

std::vector<unsigned char> readBinary(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<unsigned char> lz4Literals(const unsigned char* data, std::size_t size) {
    std::vector<unsigned char> output;
    if (size >= 15) {
        output.push_back(static_cast<unsigned char>(15U << 4U));
        std::size_t extra = size - 15;
        while (extra >= 255) {
            output.push_back(255);
            extra -= 255;
        }
        output.push_back(static_cast<unsigned char>(extra));
    } else {
        output.push_back(static_cast<unsigned char>(size << 4U));
    }
    output.insert(output.end(), data, data + size);
    return output;
}

void writeZso(const fs::path& iso_path, const fs::path& zso_path, bool compress, std::uint8_t align) {
    const std::vector<unsigned char> iso = readBinary(iso_path);
    constexpr std::uint32_t block_size = 2048;
    const std::uint64_t blocks = iso.size() / block_size;
    const std::uint64_t align_mask = align == 0 ? 0 : (std::uint64_t{1} << align) - 1U;
    std::vector<std::vector<unsigned char>> payloads(static_cast<std::size_t>(blocks));
    std::uint64_t cursor = 24 + (blocks + 1) * 4;
    std::vector<std::uint32_t> index(static_cast<std::size_t>(blocks + 1));
    for (std::uint64_t block = 0; block < blocks; ++block) {
        const unsigned char* bytes = iso.data() + block * block_size;
        payloads[static_cast<std::size_t>(block)] = compress
            ? lz4Literals(bytes, block_size)
            : std::vector<unsigned char>(bytes, bytes + block_size);
        if (align_mask != 0) {
            const std::uint64_t padded = (cursor + payloads[static_cast<std::size_t>(block)].size() + align_mask) & ~align_mask;
            payloads[static_cast<std::size_t>(block)].resize(static_cast<std::size_t>(padded - cursor), 0);
        }
        std::uint32_t entry = static_cast<std::uint32_t>(cursor >> align);
        if (!compress) {
            entry |= 0x80000000U;
        }
        index[static_cast<std::size_t>(block)] = entry;
        cursor += payloads[static_cast<std::size_t>(block)].size();
    }
    index[static_cast<std::size_t>(blocks)] = static_cast<std::uint32_t>(cursor >> align);

    std::vector<unsigned char> output;
    output.insert(output.end(), {'Z', 'I', 'S', 'O'});
    putLe32(output, 24);
    putLe32(output, static_cast<std::uint32_t>(iso.size()));
    putLe32(output, 0);
    putLe32(output, block_size);
    output.push_back(1);
    output.push_back(align);
    output.push_back(0);
    output.push_back(0);
    for (std::uint32_t entry : index) {
        putLe32(output, entry);
    }
    for (const auto& payload : payloads) {
        output.insert(output.end(), payload.begin(), payload.end());
    }
    std::ofstream file(zso_path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
}

void writeVcd(const fs::path& iso_path, const fs::path& vcd_path) {
    const std::vector<unsigned char> iso = readBinary(iso_path);
    std::ofstream file(vcd_path, std::ios::binary);
    std::vector<unsigned char> header(0x100000, 0);
    header[0] = 0x41;
    header[2] = 0xA0;
    header[7] = 0x01;
    header[8] = 0x20;
    header[0x400] = 'k';
    header[0x401] = 'H';
    header[0x402] = 'n';
    header[0x403] = ' ';
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));

    const std::size_t sectors = iso.size() / 2048;
    std::vector<unsigned char> raw(2352, 0);
    for (std::size_t sector = 0; sector < sectors; ++sector) {
        std::fill(raw.begin(), raw.end(), 0);
        std::copy_n(iso.begin() + static_cast<std::ptrdiff_t>(sector * 2048), 2048, raw.begin() + 24);
        file.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    }
}

void patchSystemCnf(const fs::path& path, std::string_view text) {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(0x0180 * 2048);
    std::string sector(2048, '\0');
    std::copy(text.begin(), text.end(), sector.begin());
    file.write(sector.data(), static_cast<std::streamsize>(sector.size()));
}

void containerTests(const fs::path& root) {
    const fs::path directory = root / "containers";
    fs::create_directories(directory);
    const fs::path iso = directory / "fixture.iso";
    createFixture(iso, "SLUS_217.20");

    const fs::path plain_zso = directory / "plain.zso";
    const fs::path compressed_zso = directory / "compressed.zso";
    writeZso(iso, plain_zso, false, 0);
    writeZso(iso, compressed_zso, true, 1);

    {
        // Close each reader before the later rename. Windows rejects a rename
        // while this process still has the image open.
        ps2br::ZsoReader plain_reader(plain_zso);
        expect(plain_reader.isOpen(), "plain ZSO should open: " + plain_reader.openError());
        if (plain_reader.isOpen()) {
            const auto identification = ps2br::identifyGame(plain_reader);
            expect(identification.success, "plain ZSO should identify: " + identification.error);
            expect(identification.game_id == "SLUS_217.20", "plain ZSO should preserve the ISO game ID");
        }
    }

    {
        ps2br::ZsoReader compressed_reader(compressed_zso);
        expect(compressed_reader.isOpen(), "compressed ZSO should open: " + compressed_reader.openError());
        if (compressed_reader.isOpen()) {
            const auto identification = ps2br::identifyGame(compressed_reader);
            expect(identification.success, "aligned LZ4 ZSO should identify: " + identification.error);
            expect(identification.game_id == "SLUS_217.20", "aligned LZ4 ZSO should preserve the ISO game ID");
        }
    }

    const fs::path bad_zso = directory / "bad.zso";
    {
        std::ofstream file(bad_zso, std::ios::binary);
        const std::string header(32, 'X');
        file.write(header.data(), static_cast<std::streamsize>(header.size()));
    }
    {
        ps2br::ZsoReader bad_reader(bad_zso);
        expect(!bad_reader.isOpen() && bad_reader.openError().find("ZISO") != std::string::npos,
               "a non-ZISO file should fail with a ZISO header error");
    }

    const fs::path vcd = directory / "fixture.vcd";
    writeVcd(iso, vcd);
    {
        ps2br::VcdReader vcd_reader(vcd);
        expect(vcd_reader.isOpen(), "mode 2 VCD should open: " + vcd_reader.openError());
        if (vcd_reader.isOpen()) {
            const auto identification = ps2br::identifyGame(vcd_reader);
            expect(identification.success, "mode 2 VCD should identify: " + identification.error);
            expect(identification.game_id == "SLUS_217.20", "VCD cooked sectors should preserve the ISO game ID");
        }
    }

    const fs::path ps1_iso = directory / "ps1.iso";
    createFixture(ps1_iso, "SLUS_217.20");
    patchSystemCnf(ps1_iso, "BOOT=cdrom:\\\\Slus_005.98;1\r\nTCB=4\r\nEVENT=10\r\n");
    {
        ps2br::IsoReader ps1_iso_reader(ps1_iso);
        const auto ps1_iso_id = ps2br::identifyGame(ps1_iso_reader);
        expect(ps1_iso_id.success && ps1_iso_id.game_id == "SLUS_005.98" && ps1_iso_id.playstation1,
               "mixed-case cdrom:\\\\ serials should normalize to the canonical ID and report PlayStation");
    }

    const fs::path ps1_vcd = directory / "ps1.vcd";
    writeVcd(ps1_iso, ps1_vcd);
    {
        ps2br::VcdReader ps1_vcd_reader(ps1_vcd);
        expect(ps1_vcd_reader.isOpen(), "PlayStation VCD should open: " + ps1_vcd_reader.openError());
        if (ps1_vcd_reader.isOpen()) {
            const auto identification = ps2br::identifyGame(ps1_vcd_reader);
            expect(identification.success && identification.game_id == "SLUS_005.98",
                   "PlayStation VCD should expose the normalized serial");
        }
    }

    ps2br::GameDatabase database;
    std::string error;
    expect(database.loadText("SLUS_217.20 Arcana Heart\r\nSLUS_005.98 Gex - Enter the Gecko\r\n", "test", error),
           "container database should load");
    ps2br::Renamer renamer(database);
    auto zso_results = renamer.scan(
        {directory, ps2br::ImageType::Zso, ps2br::NamingMode::TitleAndGameId}, {}, {});
    const auto ready_zso = std::count_if(zso_results.begin(), zso_results.end(), [](const auto& item) {
        return item.status == ps2br::ResultStatus::Ready &&
               item.destination_path.extension() == ".zso" &&
               item.destination_path.filename() == "Arcana Heart (SLUS_217.20).zso";
    });
    const auto failed_zso = std::count_if(zso_results.begin(), zso_results.end(), [](const auto& item) {
        return item.source_path.filename() == "bad.zso" && item.status == ps2br::ResultStatus::Failed;
    });
    expect(zso_results.size() == 3 && ready_zso == 2 && failed_zso == 1,
           "ZSO scan should preview both valid images and keep the invalid image visible");

    auto vcd_results = renamer.scan(
        {directory, ps2br::ImageType::Vcd, ps2br::NamingMode::TitleOnly}, {}, {});
    expect(vcd_results.size() == 2, "VCD scan should see both synthetic VCD files");
    const auto renamed = renamer.apply(vcd_results, {}, {});
    std::string rename_detail;
    for (const auto& item : vcd_results) {
        if (item.status == ps2br::ResultStatus::Renamed) {
            continue;
        }
        if (!rename_detail.empty()) {
            rename_detail += "; ";
        }
        rename_detail += item.source_path.filename().string();
        rename_detail += " ";
        rename_detail += ps2br::toString(item.status);
        rename_detail += " ";
        rename_detail += item.detail;
    }
    expect(renamed.renamed == 2,
           rename_detail.empty() ? "both synthetic VCD files should rename"
                                 : "both synthetic VCD files should rename: " + rename_detail);
    expect(fs::exists(directory / "Arcana Heart.vcd"), "PlayStation 2 VCD should rename from the database title");
    expect(fs::exists(directory / "Gex - Enter the Gecko.vcd"), "PlayStation VCD should rename from the normalized serial");
}

void exampleImageTests() {
    const fs::path examples{PS2BR_EXAMPLES_DIR};
    if (!fs::exists(examples)) {
        std::cout << "Example image tests skipped; examples directory is absent.\n";
        return;
    }

    ps2br::GameDatabase database;
    std::string error;
    expect(database.loadBuiltIn(error), "example lookup should use the built-in database: " + error);

    struct Expected {
        const char* filename;
        ps2br::ImageType type;
        const char* game_id;
        const char* title;
    };
    constexpr std::array<Expected, 10> expected{{
        {"PBPX_955.03.Gran Turismo 3 - A-spec (USA) (v1.10).zso", ps2br::ImageType::Zso,
         "PBPX_955.03", "Gran Turismo 3 - A-Spec [PS2 Bundle]"},
        {"SCUS_971.13.Ico.zso", ps2br::ImageType::Zso, "SCUS_971.13", "Ico"},
        {"SCUS_971.25.Frequency.zso", ps2br::ImageType::Zso, "SCUS_971.25", "Frequency"},
        {"SCUS_971.30.Hot Shots Golf 3 (USA).zso", ps2br::ImageType::Zso, "SCUS_971.30", "Hot Shots Golf 3"},
        {"SCUS_974.02.Killzone (USA).zso", ps2br::ImageType::Zso, "SCUS_974.02", "Killzone"},
        {"SLUS_200.17.Maximo - Ghosts to Glory [Beta].zso", ps2br::ImageType::Zso,
         "SLUS_200.17", "Maximo - Ghosts to Glory"},
        {"SLUS_200.41.ESPN International Track & Field.zso", ps2br::ImageType::Zso,
         "SLUS_200.41", "ESPN International Track & Field"},
        {"SLUS_203.12.Final Fantasy X (USA).zso", ps2br::ImageType::Zso, "SLUS_203.12", "Final Fantasy X"},
        {"Gex - Enter the Gecko.vcd", ps2br::ImageType::Vcd, "SLUS_005.98", "Gex - Enter the Gecko"},
        {"Jet Moto 3.vcd", ps2br::ImageType::Vcd, "SCUS_945.55", "Jet Moto 3"},
    }};

    bool saw_example = false;
    for (const auto& item : expected) {
        const fs::path path = examples / item.filename;
        if (!fs::exists(path)) {
            continue;
        }
        saw_example = true;
        ps2br::Identification identification;
        if (item.type == ps2br::ImageType::Zso) {
            ps2br::ZsoReader reader(path);
            expect(reader.isOpen(), std::string(item.filename) + " should open: " + reader.openError());
            if (!reader.isOpen()) {
                continue;
            }
            identification = ps2br::identifyGame(reader);
        } else {
            ps2br::VcdReader reader(path);
            expect(reader.isOpen(), std::string(item.filename) + " should open: " + reader.openError());
            if (!reader.isOpen()) {
                continue;
            }
            identification = ps2br::identifyGame(reader);
        }
        expect(identification.success && identification.game_id == item.game_id,
               std::string(item.filename) + " should identify as " + item.game_id +
                   (identification.success ? "" : ": " + identification.error));
        const auto* record = database.findFirst(item.game_id);
        expect(record != nullptr && record->title == item.title,
               std::string(item.game_id) + " should resolve to " + item.title);
    }
    if (!saw_example) {
        std::cout << "Example image tests skipped; examples directory has no ZSO or VCD files.\n";
    }
}

#if PS2BR_ENABLE_CHD && defined(PS2BR_CHDMAN_EXECUTABLE)
void chdRoundTripTest(const fs::path& root) {
    const fs::path directory = root / "chd-round-trip";
    fs::create_directories(directory);
    const fs::path iso = directory / "fixture.iso";
    const fs::path cue = directory / "fixture.cue";
    const fs::path chd = directory / "fixture.chd";
    createFixture(iso, "SLUS_217.20");

    std::ofstream cue_file(cue);
    cue_file << "FILE \"fixture.iso\" BINARY\n"
             << "  TRACK 01 MODE1/2048\n"
             << "    INDEX 01 00:00:00\n";
    cue_file.close();

    const std::string command = std::string("\"") + PS2BR_CHDMAN_EXECUTABLE +
        "\" createcd -i \"" + cue.string() + "\" -o \"" + chd.string() + "\" >/dev/null 2>&1";
    expect(std::system(command.c_str()) == 0, "chdman should create the CHD round-trip fixture");
    if (!fs::exists(chd)) return;

    ps2br::ChdReader reader(chd);
    expect(reader.isOpen(), "native CHD reader should open a chdman image: " + reader.openError());
    if (!reader.isOpen()) return;
    const auto identification = ps2br::identifyGame(reader);
    expect(identification.success, "native CHD reader should expose the original disc bytes");
    expect(identification.game_id == "SLUS_217.20", "CHD and ISO identification should return the same game ID");
    expect(reader.hasCdLayout(), "a createcd CHD should expose CD track metadata");
}
#endif

void extendUserData(const fs::path& path, std::uint64_t size) {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(static_cast<std::streamoff>(size - 1));
    file.put('\0');
    file.flush();
}

const ps2br::RenameResult* findResult(const std::vector<ps2br::RenameResult>& results, const fs::path& filename) {
    const auto found = std::find_if(results.begin(), results.end(), [&](const ps2br::RenameResult& item) {
        return item.source_path.filename() == filename;
    });
    return found == results.end() ? nullptr : &*found;
}

void autoMediaTests(const fs::path& root) {
    const fs::path plain = root / "plain-iso";
    fs::create_directories(plain);
    createFixture(plain / "arcana.iso", "SLUS_217.20");
    ps2br::GameDatabase plain_database;
    std::string error;
    expect(plain_database.loadText("SLUS_217.20 Arcana Heart\r\n", "test", error),
           "plain ISO database should load");
    ps2br::Renamer plain_renamer(plain_database);
    auto plain_results = plain_renamer.scan(
        {plain, ps2br::ImageType::Iso, ps2br::NamingMode::TitleOnly}, {}, {});
    expect(plain_results.size() == 1 && plain_results[0].status == ps2br::ResultStatus::Ready &&
               plain_results[0].destination_path == plain / "Arcana Heart.iso",
           "ISO mode should rename a DVD serial in its own folder");
    const auto plain_summary = plain_renamer.apply(plain_results, {}, {});
    expect(plain_summary.renamed == 1 && fs::exists(plain / "Arcana Heart.iso") && !fs::exists(plain / "DVD"),
           "ISO mode should not create or use a DVD folder");

    const fs::path library = root / "opl-library";
    fs::create_directories(library / "DVD");
    fs::create_directories(library / "POPS");
    createFixture(library / "arcana.iso", "SLUS_217.20");
    createFixture(library / "DVD" / "ico.iso", "SCUS_971.13");
    createFixture(library / "POPS" / "Gex - Enter the Gecko.iso", "SLUS_005.98");
    createFixture(library / "unknown.iso", "SLUS_999.99");
    createFixture(library / "big-unknown.iso", "SLUS_999.98");
    extendUserData(library / "big-unknown.iso", 737280001ULL);
    createFixture(library / "ps1-boot.iso", "SLUS_217.20");
    patchSystemCnf(library / "ps1-boot.iso", "BOOT=cdrom:\\\\SLUS_217.20;1\r\n");
    {
        std::ofstream chd(library / "broken.chd", std::ios::binary);
        chd << "not a chd";
    }

    {
        ps2br::IsoReader ps1_reader(library / "ps1-boot.iso");
        const auto ps1_boot = ps2br::identifyGame(ps1_reader);
        expect(ps1_boot.success && ps1_boot.playstation1 && ps1_boot.game_id == "SLUS_217.20",
               "a cdrom: boot path should force the PlayStation placement");
    }

    ps2br::GameDatabase database;
    expect(database.loadText(
               "SLUS_217.20 Arcana Heart\r\n"
               "SCUS_971.13 Ico\r\n"
               "SLUS_005.98 Gex - Enter the Gecko\r\n"
               "SLUS_999.99 Mystery Disc\r\n"
               "SLUS_999.98 Huge Mystery\r\n",
               "test", error),
           "auto media database should load");
    ps2br::Renamer renamer(database);

    auto iso_results = renamer.scan({library, ps2br::ImageType::Iso, ps2br::NamingMode::TitleOnly}, {}, {});
    const auto* iso_ico = findResult(iso_results, "ico.iso");
    const auto* iso_arcana = findResult(iso_results, "arcana.iso");
    expect(iso_results.size() == 6, "ISO mode should see images in CD, DVD, and POPS subfolders");
    expect(iso_ico != nullptr && iso_ico->destination_path == library / "DVD" / "Ico.iso",
           "ISO mode should rename inside the folder where the image was found");
    expect(iso_arcana != nullptr && iso_arcana->destination_path == library / "Arcana Heart.iso",
           "ISO mode should leave a root image in the selected folder");

    auto results = renamer.scan({library, ps2br::ImageType::Auto, ps2br::NamingMode::TitleOnly}, {}, {});
    const auto* arcana = findResult(results, "arcana.iso");
    const auto* ico = findResult(results, "ico.iso");
    const auto* gex = findResult(results, "Gex - Enter the Gecko.iso");
    const auto* unknown = findResult(results, "unknown.iso");
    const auto* huge = findResult(results, "big-unknown.iso");
    const auto* ps1 = findResult(results, "ps1-boot.iso");
    const auto* broken = findResult(results, "broken.chd");
    expect(results.size() == 7, "Auto mode should scan iso and chd files together");
    expect(arcana != nullptr && arcana->image_type == ps2br::ImageType::Iso &&
               arcana->status == ps2br::ResultStatus::Ready && arcana->detail == "Move to DVD" &&
               arcana->destination_path == library / "DVD" / "Arcana Heart.iso",
           "a Redump DVD serial should move into DVD");
    expect(ico != nullptr && ico->status == ps2br::ResultStatus::Ready && ico->detail == "Move to CD" &&
               ico->destination_path == library / "CD" / "Ico.iso",
           "a Redump CD serial should move into CD even when it was stored in DVD");
    expect(gex != nullptr && gex->status == ps2br::ResultStatus::Skipped &&
               gex->destination_path == library / "POPS" / "Gex - Enter the Gecko.iso",
           "a PlayStation image already in POPS with the right name should be skipped");
    expect(unknown != nullptr && unknown->status == ps2br::ResultStatus::Ready &&
               unknown->destination_path == library / "Mystery Disc.iso" &&
               unknown->detail.find("unknown") != std::string::npos,
           "a small serial missing from Redump should stay in the current folder");
    expect(huge != nullptr && huge->status == ps2br::ResultStatus::Ready && huge->detail == "Move to DVD" &&
               huge->destination_path == library / "DVD" / "Huge Mystery.iso",
           "user data past CD capacity should move into DVD");
    expect(ps1 != nullptr && ps1->status == ps2br::ResultStatus::Ready && ps1->detail == "Move to POPS" &&
               ps1->destination_path == library / "POPS" / "Arcana Heart.iso",
           "a cdrom: boot path should move into POPS even when the serial is a PlayStation 2 DVD");
    expect(broken != nullptr && broken->image_type == ps2br::ImageType::Chd &&
               broken->status == ps2br::ResultStatus::Failed,
           "Auto mode should still list CHD files");
#if !PS2BR_ENABLE_CHD
    expect(broken != nullptr && broken->detail.find("does not include CHD support") != std::string::npos,
           "Auto mode should report a build without CHD support");
#endif

    std::string rename_detail;
    const auto summary = renamer.apply(results, {}, {});
    for (const auto& item : results) {
        if (item.status == ps2br::ResultStatus::Renamed || item.status == ps2br::ResultStatus::Skipped ||
            item.status == ps2br::ResultStatus::Failed) {
            continue;
        }
        rename_detail += item.source_path.filename().string() + " " + ps2br::toString(item.status) + " " + item.detail + "; ";
    }
    expect(summary.renamed == 5 && summary.skipped == 1 && summary.failed == 1,
           rename_detail.empty() ? "Auto mode should move the five classified images"
                                 : "Auto mode should move the five classified images: " + rename_detail);
    expect(fs::exists(library / "DVD" / "Arcana Heart.iso"), "DVD image should land in DVD");
    expect(fs::exists(library / "CD" / "Ico.iso"), "CD image should land in CD");
    expect(fs::exists(library / "POPS" / "Gex - Enter the Gecko.iso"), "PlayStation image should remain in POPS");
    expect(fs::exists(library / "POPS" / "Arcana Heart.iso"), "PlayStation boot image should land in POPS");
    expect(fs::exists(library / "Mystery Disc.iso"), "unknown media should remain in the selected folder");
    expect(fs::exists(library / "DVD" / "Huge Mystery.iso"), "oversized unknown media should land in DVD");
    expect(!fs::exists(library / "DVD" / "ico.iso"), "the CD image should leave the DVD folder");

    const fs::path selected_dvd = root / "selected-dvd";
    fs::create_directories(selected_dvd / "DVD");
    createFixture(selected_dvd / "DVD" / "arcana.iso", "SLUS_217.20");
    auto nested = renamer.scan(
        {selected_dvd / "DVD", ps2br::ImageType::Auto, ps2br::NamingMode::TitleOnly}, {}, {});
    expect(nested.size() == 1 && nested[0].destination_path == selected_dvd / "DVD" / "Arcana Heart.iso",
           "selecting the DVD folder should not nest another DVD folder");

    const fs::path lowercase = root / "lowercase-dvd";
    fs::create_directories(lowercase / "dvd");
    createFixture(lowercase / "dvd" / "arcana.iso", "SLUS_217.20");
    auto reused = renamer.scan({lowercase, ps2br::ImageType::Auto, ps2br::NamingMode::TitleOnly}, {}, {});
    expect(reused.size() == 1 && reused[0].destination_path.parent_path().filename() == "dvd",
           "an existing lowercase dvd folder should be reused");
}

} // namespace

int main() {
    databaseTests();
    onlineDatabaseTests();
    const fs::path directory = fs::temp_directory_path() / "ps2br-v4-core-tests";
    std::error_code ec;
    fs::remove_all(directory, ec);
    fs::create_directories(directory, ec);
    expect(!ec, "temporary test directory should be created");
    identifierTests(directory);
    renamerTests(directory);
    safetyTests(directory);
    containerTests(directory);
    autoMediaTests(directory);
    exampleImageTests();
#if PS2BR_ENABLE_CHD && defined(PS2BR_CHDMAN_EXECUTABLE)
    chdRoundTripTest(directory);
#endif
    fs::remove_all(directory, ec);

    if (failures == 0) {
        std::cout << "All PS2 Batch Renamer V4.1 core tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
