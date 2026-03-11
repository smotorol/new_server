#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <spdlog/spdlog.h>

#include "core/log/common.h"
#include "services/world/runtime/world_runtime.h"

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    common::init_logging();

    spdlog::warn("channel_server is legacy wrapper. Use world_server as the primary target.");

    if (!svr::g_Main.InitMainThread()) {
        spdlog::error("WorldServer InitMainThread failed.");
        return 1;
    }

    svr::g_Main.MainLoop();
    return 0;
}
