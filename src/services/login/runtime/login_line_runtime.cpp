#include "services/login/runtime/login_line_runtime.h"

#include <memory>

#include <spdlog/spdlog.h>

#include "server_common/runtime/line_start_helper.h"
#include "server_common/runtime/line_client_start_helper.h"

namespace dc {

    LoginLineRuntime::LoginLineRuntime(std::uint16_t port, std::string world_host, std::uint16_t world_port)
        : port_(port)
        , world_host_(std::move(world_host))
        , world_port_(world_port)
    {
    }

    bool LoginLineRuntime::OnRuntimeInit()
    {
		dc::InitHostedLineEntry(
			client_line_,
			0,
			"login-client",
			port_,
			false,
			0);

        if (!dc::StartHostedLine(
            client_line_,
                io_,
                std::make_shared<LoginHandler>(),
            [](std::uint64_t, std::function<void()> fn) {
            if (fn) fn();
        }))
        {
            spdlog::error("LoginLineRuntime failed to start hosted line. port={}", port_);
            return false;
        }

		auto world_handler = std::make_shared<LoginWorldHandler>();
		world_handler->SetServerIdentity(
			1,              // TODO: login server id 상수로 치환
			"login",        // TODO: 설정값/상수화 가능
			port_);

        dc::InitOutboundLineEntry(
            world_line_,
            1,
            "login-world",
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
                "LoginLineRuntime failed to start outbound world line. remote={}:{}",
                world_host_,
                world_port_);
             return false;
         }

		spdlog::info(
			"LoginLineRuntime started. client_port={} world_remote={}:{}",
			port_,
			world_host_,
			world_port_);
        return true;
    }

    void LoginLineRuntime::OnBeforeIoStop()
    {
    }

    void LoginLineRuntime::OnAfterIoStop()
    {
        world_line_.host.Stop();
        client_line_.host.Stop();
        spdlog::info("LoginLineRuntime stopped.");
    }

    void LoginLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point)
    {
        // 현재는 별도 main tick 없음
    }

} // namespace dc
