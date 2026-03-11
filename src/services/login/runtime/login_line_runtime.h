#pragma once

#include <chrono>

#include "server_common/runtime/line_registry.h"
#include "server_common/runtime/line_client_start_helper.h"
#include "services/login/handler/login_handler.h"
#include "services/login/handler/login_world_handler.h"
#include "services/runtime/server_runtime_base.h"

namespace dc {

	class LoginLineRuntime final : public ServerRuntimeBase {
	public:
		LoginLineRuntime(std::uint16_t port, std::string world_host, std::uint16_t world_port);
		~LoginLineRuntime() override = default;

	private:
		bool OnRuntimeInit() override;
		void OnBeforeIoStop() override;
		void OnAfterIoStop() override;
		void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

	private:
		std::uint16_t port_ = 0;
		std::string world_host_ = "127.0.0.1";
		std::uint16_t world_port_ = 0;

		HostedLineEntry client_line_{};
		OutboundLineEntry world_line_{};
	};

} // namespace dc
