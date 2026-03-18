#include "server_common/config/aoi_config.h"

namespace dc::cfg {

    namespace {
        AoiConfig g_aoi_config{};
    }

    const AoiConfig& GetAoiConfig()
    {
        return g_aoi_config;
    }

    void SetAoiConfig(const AoiConfig& cfg)
    {
        g_aoi_config = cfg;
    }

} // namespace dc::cfg
