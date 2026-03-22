#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <spdlog/spdlog.h>
#include <atomic>

#include "core/log/common.h"
#include "services/world/runtime/world_runtime.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
	std::atomic<bool> g_shutdown_started{ false };

	void RequestWorldShutdown_() noexcept
	{
		if (g_shutdown_started.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		spdlog::warn("[world_server] shutdown requested by console close/control signal");
		svr::g_Main.ReleaseMainThread();
	}

#ifdef _WIN32
	BOOL WINAPI WorldConsoleCtrlHandler_(DWORD ctrl_type)
	{
		switch (ctrl_type) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			RequestWorldShutdown_();
			return TRUE;
		default:
			return FALSE;
		}
	}
#endif
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
	SetConsoleCtrlHandler(WorldConsoleCtrlHandler_, TRUE);
#endif
    common::init_logging();

    if (!svr::g_Main.InitMainThread()) {
        spdlog::error("WorldServer InitMainThread failed.");
        return 1;
    }

    svr::g_Main.MainLoop();
    return 0;
}
