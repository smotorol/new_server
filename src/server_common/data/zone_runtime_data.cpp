#include "server_common/data/zone_runtime_data.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace dc::zone {
namespace {
    struct RuntimeState {
        std::mutex mtx;
        ZoneRuntimeDataPack pack{};
        ZoneRuntimeDataStatus status{};
    };

    RuntimeState& GetState() noexcept
    {
        static RuntimeState state;
        return state;
    }

    template <typename T>
    bool WriteVector(std::ofstream& os, const std::vector<T>& values)
    {
        if (values.empty()) {
            return true;
        }
        os.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
        return static_cast<bool>(os);
    }

    template <typename T>
    bool ReadVector(std::ifstream& is, std::vector<T>& values, std::size_t count)
    {
        values.resize(count);
        if (count == 0) {
            return true;
        }
        is.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T)));
        return static_cast<bool>(is);
    }

    std::filesystem::path ResolveBinaryPath_(const std::filesystem::path& path)
    {
        if (!path.empty()) {
            return path;
        }
        return DefaultZoneRuntimeBinaryPath();
    }

    void UpdateStatusFromPack_(ZoneRuntimeDataStatus& status, const ZoneRuntimeDataPack& pack, const std::string& source)
    {
        status.source = source;
        status.version = pack.header.version;
        status.preload_count = pack.maps.size();
        status.portal_count = pack.portals.size();
        status.npc_count = pack.npcs.size();
        status.monster_count = pack.monsters.size();
        status.safe_count = pack.safe_regions.size();
        status.special_count = pack.special_regions.size();
        status.ready = (pack.header.magic == kZoneRuntimeDataMagic);
        status.empty = pack.maps.empty() && pack.portals.empty() && pack.npcs.empty() && pack.monsters.empty();
        if (status.ready) {
            status.last_error_reason.clear();
        }
    }
}

std::filesystem::path DefaultZoneRuntimeBinaryPath()
{
    if (const char* env = std::getenv("DC_ZONE_RUNTIME_DATA_PATH"); env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::current_path() / "resources" / "zone_runtime.bin";
}

bool WriteBinary(const std::filesystem::path& path, const ZoneRuntimeDataPack& input, std::string* error)
{
    auto pack = input;
    pack.header.magic = kZoneRuntimeDataMagic;
    pack.header.version = kZoneRuntimeDataVersion;
    pack.header.map_count = static_cast<std::uint32_t>(pack.maps.size());
    pack.header.portal_count = static_cast<std::uint32_t>(pack.portals.size());
    pack.header.npc_count = static_cast<std::uint32_t>(pack.npcs.size());
    pack.header.monster_count = static_cast<std::uint32_t>(pack.monsters.size());
    pack.header.safe_count = static_cast<std::uint32_t>(pack.safe_regions.size());
    pack.header.special_count = static_cast<std::uint32_t>(pack.special_regions.size());

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) {
        if (error) *error = "open_failed";
        return false;
    }

    os.write(reinterpret_cast<const char*>(&pack.header), static_cast<std::streamsize>(sizeof(pack.header)));
    const bool ok =
        static_cast<bool>(os) &&
        WriteVector(os, pack.maps) &&
        WriteVector(os, pack.portals) &&
        WriteVector(os, pack.npcs) &&
        WriteVector(os, pack.monsters) &&
        WriteVector(os, pack.safe_regions) &&
        WriteVector(os, pack.special_regions);

    if (!ok) {
        if (error) *error = "write_failed";
        return false;
    }
    return true;
}

bool ReadBinary(const std::filesystem::path& path, ZoneRuntimeDataPack& out, std::string* error)
{
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        if (error) *error = "open_failed";
        return false;
    }

    ZoneRuntimeDataHeader header{};
    is.read(reinterpret_cast<char*>(&header), static_cast<std::streamsize>(sizeof(header)));
    if (!is) {
        if (error) *error = "header_read_failed";
        return false;
    }
    if (header.magic != kZoneRuntimeDataMagic) {
        if (error) *error = "bad_magic";
        return false;
    }
    if (header.version != kZoneRuntimeDataVersion) {
        if (error) *error = "unsupported_version";
        return false;
    }

    ZoneRuntimeDataPack pack{};
    pack.header = header;
    const bool ok =
        ReadVector(is, pack.maps, header.map_count) &&
        ReadVector(is, pack.portals, header.portal_count) &&
        ReadVector(is, pack.npcs, header.npc_count) &&
        ReadVector(is, pack.monsters, header.monster_count) &&
        ReadVector(is, pack.safe_regions, header.safe_count) &&
        ReadVector(is, pack.special_regions, header.special_count);

    if (!ok) {
        if (error) *error = "body_read_failed";
        return false;
    }

    out = std::move(pack);
    return true;
}

bool ZoneRuntimeDataStore::LoadFromBinary(const std::filesystem::path& path)
{
    auto resolved = ResolveBinaryPath_(path);
    ZoneRuntimeDataPack pack{};
    std::string error;
    if (!ReadBinary(resolved, pack, &error)) {
        auto& state = GetState();
        std::lock_guard lk(state.mtx);
        state.pack = {};
        state.status = {};
        state.status.source = resolved.string();
        state.status.last_error_reason = error;
        state.status.ready = false;
        state.status.empty = true;
        return false;
    }

    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    state.pack = std::move(pack);
    state.status = {};
    UpdateStatusFromPack_(state.status, state.pack, resolved.string());
    return true;
}

std::vector<ZoneMapRecord> ZoneRuntimeDataStore::GetMapRecords(std::uint32_t zone_id)
{
    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    std::vector<ZoneMapRecord> out;
    out.reserve(state.pack.maps.size());
    for (const auto& row : state.pack.maps) {
        if (zone_id == 0 || row.zone_id == zone_id) {
            out.push_back(row);
        }
    }
    return out;
}

const ZonePortalRecord* ZoneRuntimeDataStore::FindTriggeredPortal(std::uint32_t zone_id, std::uint32_t map_id, std::int32_t x, std::int32_t y)
{
    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    for (const auto& row : state.pack.portals) {
        if (row.zone_id != zone_id) {
            continue;
        }
        if (row.map_id != 0 && map_id != 0 && row.map_id != map_id) {
            continue;
        }
        const auto dx = static_cast<double>(x - row.center_x);
        const auto dy = static_cast<double>(y - row.center_z);
        const auto rr = static_cast<double>(std::max(0, row.radius));
        if ((dx * dx + dy * dy) <= (rr * rr)) {
            return &row;
        }
    }
    return nullptr;
}

std::vector<ZoneNpcRecord> ZoneRuntimeDataStore::GetNpcOverlay(std::uint32_t zone_id)
{
    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    std::vector<ZoneNpcRecord> out;
    for (const auto& row : state.pack.npcs) {
        if (row.zone_id == zone_id) {
            out.push_back(row);
        }
    }
    return out;
}

std::vector<ZoneMonsterRecord> ZoneRuntimeDataStore::GetMonsterOverlay(std::uint32_t zone_id)
{
    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    std::vector<ZoneMonsterRecord> out;
    for (const auto& row : state.pack.monsters) {
        if (row.zone_id == zone_id) {
            out.push_back(row);
        }
    }
    return out;
}

std::vector<ZonePortalRecord> ZoneRuntimeDataStore::GetPortalOverlay(std::uint32_t zone_id)
{
    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    std::vector<ZonePortalRecord> out;
    for (const auto& row : state.pack.portals) {
        if (row.zone_id == zone_id) {
            out.push_back(row);
        }
    }
    return out;
}

ZoneRuntimeDataStatus ZoneRuntimeDataStore::SnapshotStatus()
{
    auto& state = GetState();
    std::lock_guard lk(state.mtx);
    return state.status;
}

} // namespace dc::zone


