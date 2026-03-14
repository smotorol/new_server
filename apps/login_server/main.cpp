#include <spdlog/spdlog.h>

#include "core/log/common.h"
#include "services/login/runtime/login_line_runtime.h"

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    common::init_logging();

    constexpr std::uint16_t kLoginPort = 26788;
    constexpr std::uint16_t kWorldPort = 27788;
    constexpr std::uint16_t kAccountPort = 27780;
    dc::LoginLineRuntime runtime(
        kLoginPort,
        "127.0.0.1",
        kWorldPort,
        "127.0.0.1",
        kAccountPort);
    if (!runtime.InitMainThread()) {
        spdlog::error("LoginLineRuntime InitMainThread failed.");
        return 1;
    }

    runtime.MainLoop();
    return 0;
}
