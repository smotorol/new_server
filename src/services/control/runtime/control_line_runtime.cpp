#include "services/control/runtime/control_line_runtime.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <spdlog/spdlog.h>

#include "server_common/runtime/line_start_helper.h"
#include "server_common/runtime/line_client_start_helper.h"

namespace dc {

    ControlLineRuntime::ControlLineRuntime(std::uint16_t port, std::string world_host, std::uint16_t world_port)
        : port_(port)
		, world_host_(std::move(world_host))
		, world_port_(world_port)
    {
    }

	bool ControlLineRuntime::OnRuntimeInit()
	{
		dc::InitHostedLineEntry(
			client_line_,
			0,
			"control-client",
			port_,
			false,
			0);

		if (!dc::StartHostedLine(
			client_line_,
			io_,
			std::make_shared<ControlHandler>(),
			[](std::uint64_t, std::function<void()> fn) {
			if (fn) fn();
		}))
		{
			spdlog::error("ControlLineRuntime failed to start hosted line. port={}", port_);
			return false;
		}

		auto world_handler = std::make_shared<ControlWorldHandler>();
		world_handler->SetServerIdentity(
			2,              // TODO: control server id 상수로 치환
			"control",      // TODO: 설정값/상수화 가능
			port_);

		dc::InitOutboundLineEntry(
			world_line_,
			1,
			"control-world",
			world_host_,
			world_port_,
			true,
			1000,
			10000);

		if (!dc::StartOutboundLine(
			world_line_,
			io_,
			world_handler,
			[](std::uint64_t, std::function<void()> fn) {
			if (fn) fn();
		}))
		{
			spdlog::error(
				"ControlLineRuntime failed to start outbound world line. remote={}:{}",
				world_host_,
				world_port_);
			return false;
		}

		spdlog::info(
			"ControlLineRuntime started. client_port={} world_remote={}:{}",
			port_,
			world_host_,
			world_port_);
		return true;
	}

    void ControlLineRuntime::OnBeforeIoStop()
    {
    }

    void ControlLineRuntime::OnAfterIoStop()
    {
        world_line_.host.Stop();
        client_line_.host.Stop();
        spdlog::info("ControlLineRuntime stopped.");
    }

    void ControlLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point)
    {
        // 현재는 별도 main tick 없음
    }

} // namespace dc
