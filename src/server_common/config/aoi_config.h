#pragma once

#include <cstdint>
#include "services/world/actors/zone_actor.h"

namespace dc::cfg {

    struct AoiConfig {
        svr::Vec2i map_size{ 2000, 2000 };
        // Coordinate rule: 1 world unit == 1 meter.
        // WORLD_SIGHT_UNIT is only the AOI cell granularity in meters.
        int world_sight_unit = svr::ZoneActor::kCellSize;
        // 5 cells * 10 meters-per-cell => 50 meter AOI radius by default.
        int aoi_radius_cells = 5;
    };

    // 전역 공용 설정 접근
    const AoiConfig& GetAoiConfig();

    // 초기화/재설정이 필요하면 사용
    void SetAoiConfig(const AoiConfig& cfg);

} // namespace dc::cfg
