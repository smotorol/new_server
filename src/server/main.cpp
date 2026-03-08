#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <spdlog/spdlog.h>

#include "../common/common.h"
#include "mainthread.h"

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    common::init_logging();

    if (!svr::g_Main.InitMainThread()) {
        spdlog::error("LogServer InitMainThread failed.");
        return 1;
    }

    svr::g_Main.MainLoop();
    return 0;
}
