#include <spdlog/spdlog.h>

#include "core/log/common.h"
#include "services/control/runtime/control_line_runtime.h"

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    common::init_logging();

    constexpr std::uint16_t kControlPort = 26789;
    constexpr std::uint16_t kWorldPort = 27789; // TODO: 실제 world control port 값으로 수정

    dc::ControlLineRuntime runtime(kControlPort, "127.0.0.1", kWorldPort);
    if (!runtime.InitMainThread()) {
        spdlog::error("ControlLineRuntime InitMainThread failed.");
        return 1;
    }

    runtime.MainLoop();
    return 0;
}
