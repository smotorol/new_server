#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dc::zone {

inline constexpr std::uint32_t kZoneRuntimeDataMagic = 0x31444E5Au; // ZND1
inline constexpr std::uint32_t kZoneRuntimeDataVersion = 1u;

#pragma pack(push, 1)
struct ZoneRuntimeDataHeader {
    std::uint32_t magic = kZoneRuntimeDataMagic;
    std::uint32_t version = kZoneRuntimeDataVersion;
    std::uint32_t map_count = 0;
    std::uint32_t portal_count = 0;
    std::uint32_t npc_count = 0;
    std::uint32_t monster_count = 0;
    std::uint32_t safe_count = 0;
    std::uint32_t special_count = 0;
};

struct ZoneMapRecord {
    std::uint32_t zone_id = 0;
    std::uint32_t map_id = 0;
};

struct ZonePortalRecord {
    std::uint32_t zone_id = 0;
    std::uint32_t map_id = 0;
    std::uint32_t region_id = 0;
    std::int32_t value01 = 0;
    std::int32_t value02 = 0;
    std::int32_t value03 = 0;
    std::int32_t value04 = 0;
    std::int32_t center_x = 0;
    std::int32_t center_y = 0;
    std::int32_t center_z = 0;
    std::int32_t radius = 0;
    std::uint32_t dest_zone_id = 0;
    std::uint32_t dest_map_id = 0;
    std::int32_t dest_x = 0;
    std::int32_t dest_y = 0;
    std::int32_t dest_z = 0;
};

struct ZoneNpcRecord {
    std::uint32_t zone_id = 0;
    std::uint32_t map_id = 0;
    std::uint32_t region_id = 0;
    std::int32_t value01 = 0;
    std::int32_t value02 = 0;
    std::int32_t value03 = 0;
    std::int32_t value04 = 0;
    std::int32_t center_x = 0;
    std::int32_t center_y = 0;
    std::int32_t center_z = 0;
    std::int32_t radius = 0;
};

struct ZoneMonsterRecord {
    std::uint32_t zone_id = 0;
    std::uint32_t map_id = 0;
    std::uint32_t region_id = 0;
    std::int32_t value01 = 0;
    std::int32_t value02 = 0;
    std::int32_t value03 = 0;
    std::int32_t value04 = 0;
    std::int32_t center_x = 0;
    std::int32_t center_y = 0;
    std::int32_t center_z = 0;
    std::int32_t radius = 0;
};

struct ZoneSafeRecord {
    std::uint32_t zone_id = 0;
    std::uint32_t map_id = 0;
    std::uint32_t region_id = 0;
    std::int32_t value01 = 0;
    std::int32_t value02 = 0;
    std::int32_t value03 = 0;
    std::int32_t value04 = 0;
    std::int32_t center_x = 0;
    std::int32_t center_y = 0;
    std::int32_t center_z = 0;
    std::int32_t radius = 0;
};

struct ZoneSpecialRecord {
    std::uint32_t zone_id = 0;
    std::uint32_t map_id = 0;
    std::uint32_t region_id = 0;
    std::int32_t value01 = 0;
    std::int32_t value02 = 0;
    std::int32_t value03 = 0;
    std::int32_t value04 = 0;
    std::int32_t center_x = 0;
    std::int32_t center_y = 0;
    std::int32_t center_z = 0;
    std::int32_t radius = 0;
};
#pragma pack(pop)

struct ZoneRuntimeDataPack {
    ZoneRuntimeDataHeader header{};
    std::vector<ZoneMapRecord> maps;
    std::vector<ZonePortalRecord> portals;
    std::vector<ZoneNpcRecord> npcs;
    std::vector<ZoneMonsterRecord> monsters;
    std::vector<ZoneSafeRecord> safe_regions;
    std::vector<ZoneSpecialRecord> special_regions;
};

struct ZoneRuntimeDataStatus {
    std::string source = "empty";
    std::uint32_t version = 0;
    std::size_t preload_count = 0;
    std::size_t portal_count = 0;
    std::size_t npc_count = 0;
    std::size_t monster_count = 0;
    std::size_t safe_count = 0;
    std::size_t special_count = 0;
    bool ready = false;
    bool empty = true;
    std::string last_error_reason;
};

std::filesystem::path DefaultZoneRuntimeBinaryPath();
bool WriteBinary(const std::filesystem::path& path, const ZoneRuntimeDataPack& pack, std::string* error = nullptr);
bool ReadBinary(const std::filesystem::path& path, ZoneRuntimeDataPack& out, std::string* error = nullptr);

class ZoneRuntimeDataStore {
public:
    static bool LoadFromBinary(const std::filesystem::path& path);
    static std::vector<ZoneMapRecord> GetMapRecords(std::uint32_t zone_id = 0);
    static const ZonePortalRecord* FindTriggeredPortal(std::uint32_t zone_id, std::uint32_t map_id, std::int32_t x, std::int32_t y);
    static std::vector<ZoneNpcRecord> GetNpcOverlay(std::uint32_t zone_id);
    static std::vector<ZoneMonsterRecord> GetMonsterOverlay(std::uint32_t zone_id);
    static std::vector<ZonePortalRecord> GetPortalOverlay(std::uint32_t zone_id);
    static ZoneRuntimeDataStatus SnapshotStatus();
};

} // namespace dc::zone


