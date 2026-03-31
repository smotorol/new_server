#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <tuple>
#include <vector>

#include "server_common/data/zone_runtime_data.h"

namespace fs = std::filesystem;

namespace {
    struct WorldRegionRow {
        std::int32_t value01 = 0;
        std::int32_t value02 = 0;
        std::int32_t value03 = 0;
        std::int32_t value04 = 0;
        std::int32_t center_x = 0;
        std::int32_t center_y = 0;
        std::int32_t center_z = 0;
        std::int32_t radius = 0;
    };

    struct Options {
        fs::path legacy_data = fs::path(R"(G:\Programing\Work\12sky1\12sky1\S07_TS_ZONE\DATA)");
        fs::path csv_dir = fs::current_path() / "data_src" / "zone_csv";
        fs::path bin_path = dc::zone::DefaultZoneRuntimeBinaryPath();
        bool extract = true;
        bool build = true;
    };

    std::string ToUpper(std::string v)
    {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return v;
    }

    bool StartsWith(std::string_view value, std::string_view prefix)
    {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    std::optional<std::uint32_t> ParseZoneIdFromStem(const std::string& stem)
    {
        if (stem.size() < 4 || stem[0] != 'Z') {
            return std::nullopt;
        }
        const auto digits = stem.substr(1, 3);
        if (!std::isdigit(static_cast<unsigned char>(digits[0])) || !std::isdigit(static_cast<unsigned char>(digits[1])) || !std::isdigit(static_cast<unsigned char>(digits[2]))) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(std::stoi(digits));
    }

    bool ReadWorldRegionFile(const fs::path& path, std::vector<WorldRegionRow>& out, std::string* error)
    {
        std::ifstream is(path, std::ios::binary);
        if (!is) {
            if (error) *error = "open_failed";
            return false;
        }
        std::int32_t count = 0;
        is.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!is || count < 0) {
            if (error) *error = "header_read_failed";
            return false;
        }
        out.resize(static_cast<std::size_t>(count));
        if (count > 0) {
            is.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sizeof(WorldRegionRow) * out.size()));
            if (!is) {
                if (error) *error = "body_read_failed";
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> SplitCsv(const std::string& line)
    {
        std::vector<std::string> out;
        std::string cell;
        std::stringstream ss(line);
        while (std::getline(ss, cell, ',')) {
            out.push_back(cell);
        }
        return out;
    }

    template <typename T>
    bool ParseUnsigned(const std::string& value, T& out)
    {
        try {
            out = static_cast<T>(std::stoull(value));
            return true;
        }
        catch (...) {
            return false;
        }
    }

    template <typename T>
    bool ParseSigned(const std::string& value, T& out)
    {
        try {
            out = static_cast<T>(std::stoll(value));
            return true;
        }
        catch (...) {
            return false;
        }
    }

    void ResolvePortalDestinations(dc::zone::ZoneRuntimeDataPack& pack)
    {
        for (auto& portal : pack.portals) {
            if (portal.dest_zone_id == 0 && portal.value02 > 0) {
                portal.dest_zone_id = static_cast<std::uint32_t>(portal.value02);
            }
            if (portal.dest_map_id == 0) {
                portal.dest_map_id = (portal.dest_zone_id != 0) ? portal.dest_zone_id : portal.map_id;
            }
            const dc::zone::ZonePortalRecord* reciprocal = nullptr;
            for (const auto& candidate : pack.portals) {
                if (candidate.zone_id != portal.dest_zone_id) {
                    continue;
                }
                if (candidate.value02 != static_cast<std::int32_t>(portal.zone_id)) {
                    continue;
                }
                reciprocal = &candidate;
                break;
            }
            if (reciprocal != nullptr) {
                portal.dest_x = reciprocal->center_x;
                portal.dest_y = reciprocal->center_z;
                portal.dest_z = reciprocal->center_y;
            }
            else {
                portal.dest_x = portal.center_x;
                portal.dest_y = portal.center_z;
                portal.dest_z = portal.center_y;
            }
        }
    }

    dc::zone::ZoneRuntimeDataPack ExtractFromLegacy(const fs::path& legacy_data)
    {
        dc::zone::ZoneRuntimeDataPack pack{};
        std::unordered_set<std::uint32_t> seen_maps;

        for (const auto& entry : fs::directory_iterator(legacy_data)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto ext = ToUpper(entry.path().extension().string());
            const auto stem = entry.path().stem().string();
            const auto zone_id_opt = ParseZoneIdFromStem(stem);
            if (!zone_id_opt.has_value()) {
                continue;
            }
            const auto zone_id = *zone_id_opt;

            if (ext == ".WM") {
                if (seen_maps.insert(zone_id).second) {
                    pack.maps.push_back(dc::zone::ZoneMapRecord{ zone_id, zone_id });
                }
                continue;
            }
            if (ext != ".WREGION") {
                continue;
            }

            std::vector<WorldRegionRow> rows;
            std::string error;
            if (!ReadWorldRegionFile(entry.path(), rows, &error)) {
                std::cerr << "skip unreadable wregion: " << entry.path().string() << " reason=" << error << "\n";
                continue;
            }

            const auto upper_stem = ToUpper(stem);
            auto append_common = [&](auto& record, std::size_t idx, const WorldRegionRow& row) {
                record.zone_id = zone_id;
                record.map_id = zone_id;
                record.region_id = static_cast<std::uint32_t>(idx + 1);
                record.value01 = row.value01;
                record.value02 = row.value02;
                record.value03 = row.value03;
                record.value04 = row.value04;
                record.center_x = row.center_x;
                record.center_y = row.center_y;
                record.center_z = row.center_z;
                record.radius = row.radius;
            };

            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto& row = rows[i];
                if (upper_stem.find("_ZONEMOVEREGION") != std::string::npos) {
                    dc::zone::ZonePortalRecord record{};
                    append_common(record, i, row);
                    if (row.value02 > 0) {
                        record.dest_zone_id = static_cast<std::uint32_t>(row.value02);
                        record.dest_map_id = static_cast<std::uint32_t>(row.value02);
                    }
                    pack.portals.push_back(record);
                }
                else if (upper_stem.find("_SUMMONNPC") != std::string::npos || upper_stem.find("_SUMMONGUARD") != std::string::npos) {
                    dc::zone::ZoneNpcRecord record{};
                    append_common(record, i, row);
                    pack.npcs.push_back(record);
                }
                else if (upper_stem.find("_SUMMONMONSTER") != std::string::npos) {
                    dc::zone::ZoneMonsterRecord record{};
                    append_common(record, i, row);
                    pack.monsters.push_back(record);
                }
                else if (upper_stem.find("_ZONESAFEREGION") != std::string::npos) {
                    dc::zone::ZoneSafeRecord record{};
                    append_common(record, i, row);
                    pack.safe_regions.push_back(record);
                }
                else if (upper_stem.find("_SPECIALREGION") != std::string::npos) {
                    dc::zone::ZoneSpecialRecord record{};
                    append_common(record, i, row);
                    pack.special_regions.push_back(record);
                }
            }
        }

        std::sort(pack.maps.begin(), pack.maps.end(), [](const auto& a, const auto& b) { return a.zone_id < b.zone_id; });
        std::sort(pack.portals.begin(), pack.portals.end(), [](const auto& a, const auto& b) { return std::tie(a.zone_id, a.region_id) < std::tie(b.zone_id, b.region_id); });
        ResolvePortalDestinations(pack);
        return pack;
    }

    template <typename T, typename WriterT>
    void WriteRegionCsv(const fs::path& path, std::string_view header, const std::vector<T>& rows, WriterT writer)
    {
        std::ofstream os(path, std::ios::trunc);
        os << header << "\n";
        for (const auto& row : rows) {
            writer(os, row);
            os << "\n";
        }
    }

    void WriteCsvPack(const fs::path& dir, const dc::zone::ZoneRuntimeDataPack& pack)
    {
        fs::create_directories(dir);

        WriteRegionCsv(dir / "maps.csv", "zone_id,map_id", pack.maps, [](std::ofstream& os, const dc::zone::ZoneMapRecord& row) {
            os << row.zone_id << ',' << row.map_id;
        });
        WriteRegionCsv(dir / "portal_regions.csv", "zone_id,map_id,region_id,value01,value02,value03,value04,center_x,center_y,center_z,radius,dest_zone_id,dest_map_id,dest_x,dest_y,dest_z", pack.portals, [](std::ofstream& os, const dc::zone::ZonePortalRecord& row) {
            os << row.zone_id << ',' << row.map_id << ',' << row.region_id << ',' << row.value01 << ',' << row.value02 << ',' << row.value03 << ',' << row.value04 << ',' << row.center_x << ',' << row.center_y << ',' << row.center_z << ',' << row.radius << ',' << row.dest_zone_id << ',' << row.dest_map_id << ',' << row.dest_x << ',' << row.dest_y << ',' << row.dest_z;
        });
        WriteRegionCsv(dir / "summon_npc_regions.csv", "zone_id,map_id,region_id,value01,value02,value03,value04,center_x,center_y,center_z,radius", pack.npcs, [](std::ofstream& os, const dc::zone::ZoneNpcRecord& row) {
            os << row.zone_id << ',' << row.map_id << ',' << row.region_id << ',' << row.value01 << ',' << row.value02 << ',' << row.value03 << ',' << row.value04 << ',' << row.center_x << ',' << row.center_y << ',' << row.center_z << ',' << row.radius;
        });
        WriteRegionCsv(dir / "summon_monster_regions.csv", "zone_id,map_id,region_id,value01,value02,value03,value04,center_x,center_y,center_z,radius", pack.monsters, [](std::ofstream& os, const dc::zone::ZoneMonsterRecord& row) {
            os << row.zone_id << ',' << row.map_id << ',' << row.region_id << ',' << row.value01 << ',' << row.value02 << ',' << row.value03 << ',' << row.value04 << ',' << row.center_x << ',' << row.center_y << ',' << row.center_z << ',' << row.radius;
        });
        WriteRegionCsv(dir / "safe_regions.csv", "zone_id,map_id,region_id,value01,value02,value03,value04,center_x,center_y,center_z,radius", pack.safe_regions, [](std::ofstream& os, const dc::zone::ZoneSafeRecord& row) {
            os << row.zone_id << ',' << row.map_id << ',' << row.region_id << ',' << row.value01 << ',' << row.value02 << ',' << row.value03 << ',' << row.value04 << ',' << row.center_x << ',' << row.center_y << ',' << row.center_z << ',' << row.radius;
        });
        WriteRegionCsv(dir / "special_regions.csv", "zone_id,map_id,region_id,value01,value02,value03,value04,center_x,center_y,center_z,radius", pack.special_regions, [](std::ofstream& os, const dc::zone::ZoneSpecialRecord& row) {
            os << row.zone_id << ',' << row.map_id << ',' << row.region_id << ',' << row.value01 << ',' << row.value02 << ',' << row.value03 << ',' << row.value04 << ',' << row.center_x << ',' << row.center_y << ',' << row.center_z << ',' << row.radius;
        });
    }

    template <typename T, typename ParserT>
    void LoadRegionCsv(const fs::path& path, std::vector<T>& out, ParserT parser)
    {
        std::ifstream is(path);
        if (!is) {
            return;
        }
        std::string line;
        std::getline(is, line);
        while (std::getline(is, line)) {
            if (line.empty()) {
                continue;
            }
            auto cells = SplitCsv(line);
            T row{};
            if (parser(cells, row)) {
                out.push_back(row);
            }
        }
    }

    dc::zone::ZoneRuntimeDataPack LoadCsvPack(const fs::path& dir)
    {
        dc::zone::ZoneRuntimeDataPack pack{};
        LoadRegionCsv(dir / "maps.csv", pack.maps, [](const std::vector<std::string>& cells, dc::zone::ZoneMapRecord& row) {
            return cells.size() >= 2 && ParseUnsigned(cells[0], row.zone_id) && ParseUnsigned(cells[1], row.map_id);
        });
        LoadRegionCsv(dir / "portal_regions.csv", pack.portals, [](const std::vector<std::string>& cells, dc::zone::ZonePortalRecord& row) {
            return cells.size() >= 16 &&
                ParseUnsigned(cells[0], row.zone_id) && ParseUnsigned(cells[1], row.map_id) && ParseUnsigned(cells[2], row.region_id) &&
                ParseSigned(cells[3], row.value01) && ParseSigned(cells[4], row.value02) && ParseSigned(cells[5], row.value03) && ParseSigned(cells[6], row.value04) &&
                ParseSigned(cells[7], row.center_x) && ParseSigned(cells[8], row.center_y) && ParseSigned(cells[9], row.center_z) && ParseSigned(cells[10], row.radius) &&
                ParseUnsigned(cells[11], row.dest_zone_id) && ParseUnsigned(cells[12], row.dest_map_id) && ParseSigned(cells[13], row.dest_x) && ParseSigned(cells[14], row.dest_y) && ParseSigned(cells[15], row.dest_z);
        });
        LoadRegionCsv(dir / "summon_npc_regions.csv", pack.npcs, [](const std::vector<std::string>& cells, dc::zone::ZoneNpcRecord& row) {
            return cells.size() >= 11 &&
                ParseUnsigned(cells[0], row.zone_id) && ParseUnsigned(cells[1], row.map_id) && ParseUnsigned(cells[2], row.region_id) &&
                ParseSigned(cells[3], row.value01) && ParseSigned(cells[4], row.value02) && ParseSigned(cells[5], row.value03) && ParseSigned(cells[6], row.value04) &&
                ParseSigned(cells[7], row.center_x) && ParseSigned(cells[8], row.center_y) && ParseSigned(cells[9], row.center_z) && ParseSigned(cells[10], row.radius);
        });
        LoadRegionCsv(dir / "summon_monster_regions.csv", pack.monsters, [](const std::vector<std::string>& cells, dc::zone::ZoneMonsterRecord& row) {
            return cells.size() >= 11 &&
                ParseUnsigned(cells[0], row.zone_id) && ParseUnsigned(cells[1], row.map_id) && ParseUnsigned(cells[2], row.region_id) &&
                ParseSigned(cells[3], row.value01) && ParseSigned(cells[4], row.value02) && ParseSigned(cells[5], row.value03) && ParseSigned(cells[6], row.value04) &&
                ParseSigned(cells[7], row.center_x) && ParseSigned(cells[8], row.center_y) && ParseSigned(cells[9], row.center_z) && ParseSigned(cells[10], row.radius);
        });
        LoadRegionCsv(dir / "safe_regions.csv", pack.safe_regions, [](const std::vector<std::string>& cells, dc::zone::ZoneSafeRecord& row) {
            return cells.size() >= 11 &&
                ParseUnsigned(cells[0], row.zone_id) && ParseUnsigned(cells[1], row.map_id) && ParseUnsigned(cells[2], row.region_id) &&
                ParseSigned(cells[3], row.value01) && ParseSigned(cells[4], row.value02) && ParseSigned(cells[5], row.value03) && ParseSigned(cells[6], row.value04) &&
                ParseSigned(cells[7], row.center_x) && ParseSigned(cells[8], row.center_y) && ParseSigned(cells[9], row.center_z) && ParseSigned(cells[10], row.radius);
        });
        LoadRegionCsv(dir / "special_regions.csv", pack.special_regions, [](const std::vector<std::string>& cells, dc::zone::ZoneSpecialRecord& row) {
            return cells.size() >= 11 &&
                ParseUnsigned(cells[0], row.zone_id) && ParseUnsigned(cells[1], row.map_id) && ParseUnsigned(cells[2], row.region_id) &&
                ParseSigned(cells[3], row.value01) && ParseSigned(cells[4], row.value02) && ParseSigned(cells[5], row.value03) && ParseSigned(cells[6], row.value04) &&
                ParseSigned(cells[7], row.center_x) && ParseSigned(cells[8], row.center_y) && ParseSigned(cells[9], row.center_z) && ParseSigned(cells[10], row.radius);
        });
        pack.header.magic = dc::zone::kZoneRuntimeDataMagic;
        pack.header.version = dc::zone::kZoneRuntimeDataVersion;
        return pack;
    }

    Options ParseOptions(int argc, char** argv)
    {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&](fs::path& target) {
                if (i + 1 < argc) {
                    target = fs::path(argv[++i]);
                }
            };
            if (arg == "--legacy-data") next(options.legacy_data);
            else if (arg == "--csv-out") next(options.csv_dir);
            else if (arg == "--csv-in") next(options.csv_dir);
            else if (arg == "--bin-out") next(options.bin_path);
            else if (arg == "--extract-only") { options.extract = true; options.build = false; }
            else if (arg == "--build-only") { options.extract = false; options.build = true; }
        }
        return options;
    }
}

int main(int argc, char** argv)
{
    const auto options = ParseOptions(argc, argv);
    try {
        if (options.extract) {
            auto pack = ExtractFromLegacy(options.legacy_data);
            WriteCsvPack(options.csv_dir, pack);
            std::cout << "zone_data_builder extract complete: legacy_data=" << options.legacy_data.string()
                << " csv_dir=" << options.csv_dir.string()
                << " maps=" << pack.maps.size()
                << " portals=" << pack.portals.size()
                << " npcs=" << pack.npcs.size()
                << " monsters=" << pack.monsters.size() << "\n";
            if (!options.build) {
                return 0;
            }
        }

        auto pack = LoadCsvPack(options.csv_dir);
        std::string error;
        if (!dc::zone::WriteBinary(options.bin_path, pack, &error)) {
            std::cerr << "zone_data_builder build failed: bin_path=" << options.bin_path.string() << " reason=" << error << "\n";
            return 1;
        }

        std::cout << "zone_data_builder build complete: csv_dir=" << options.csv_dir.string()
            << " bin_path=" << options.bin_path.string()
            << " maps=" << pack.maps.size()
            << " portals=" << pack.portals.size()
            << " npcs=" << pack.npcs.size()
            << " monsters=" << pack.monsters.size()
            << " safe=" << pack.safe_regions.size()
            << " special=" << pack.special_regions.size() << "\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "zone_data_builder exception: " << e.what() << "\n";
        return 1;
    }
}






