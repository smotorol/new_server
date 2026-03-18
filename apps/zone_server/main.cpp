#include <spdlog/spdlog.h>

#include "core/log/common.h"
#include "services/zone/runtime/zone_runtime.h"

int main()
{
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif
	common::init_logging();

	if (!svr::g_ZoneMain.InitMainThread()) {
		spdlog::error("ZoneServer InitMainThread failed.");
		return 1;
	}

	svr::g_ZoneMain.MainLoop();
	return 0;
}
