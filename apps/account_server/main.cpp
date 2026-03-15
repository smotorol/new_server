#include <spdlog/spdlog.h>

#include "core/log/common.h"
#include "services/account/runtime/account_line_runtime.h"

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    common::init_logging();

    constexpr std::uint16_t kAccountLoginPort = 27780;
    constexpr std::uint16_t kAccountWorldPort = 27781;

    dc::AccountLineRuntime runtime(kAccountLoginPort, kAccountWorldPort);
    if (!runtime.InitMainThread()) {
        spdlog::error("AccountLineRuntime InitMainThread failed.");
        return 1;
    }

    runtime.MainLoop();
    return 0;
}
