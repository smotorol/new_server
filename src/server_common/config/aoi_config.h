#pragma once

#include <cstdint>
#include "services/world/actors/zone_actor.h"

namespace dc::cfg {

    struct AoiConfig {
        svr::Vec2i map_size{ 2000, 2000 };
        int world_sight_unit = svr::ZoneActor::kCellSize;
        int aoi_radius_cells = 1; // 1 => 3x3
    };

    // 전역 공용 설정 접근
    const AoiConfig& GetAoiConfig();

    // 초기화/재설정이 필요하면 사용
    void SetAoiConfig(const AoiConfig& cfg);

} // namespace dc::cfg
