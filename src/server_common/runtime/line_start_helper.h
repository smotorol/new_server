#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <spdlog/spdlog.h>

#include "server_common/handler/service_line_handler_base.h"
#include "server_common/runtime/line_registry.h"

namespace dc {
	inline void InitHostedLineEntry(
		HostedLineEntry& line,
		std::uint32_t id,
		std::string_view name,
		std::uint16_t port,
		bool verbose_session_log = false,
		std::size_t max_sessions = 0)
	{
		line.desc.id = id;
		line.desc.name = name;
		line.desc.port = port;
		line.desc.verbose_session_log = verbose_session_log;
		line.desc.max_sessions = max_sessions;
	}

	inline void InitSingleHostedLine(
		HostedLineEntry& line,
		std::string_view name,
		std::uint16_t port)
	{
		InitHostedLineEntry(
			line,
			0,
			name,
			port,
			false,
			0);
	}

	inline bool StartHostedLine(
		HostedLineEntry& line,
		boost::asio::io_context& io,
		std::shared_ptr<ServiceLineHandlerBase> handler,
		const std::function<void(std::uint64_t, std::function<void()>)>& dispatch)
	{
		if (!line.host.Start(io,
			dc::LineHost::Config{
				.port = line.desc.port,
				.name = line.desc.name,
				.verbose_session_log = line.desc.verbose_session_log,
				.max_sessions = line.desc.max_sessions,
				.session_open_policy = [](std::uint32_t, std::uint64_t) {
					return true;
				},
				.on_session_open = [](std::uint32_t, std::uint64_t) {},
				.on_session_close = [](std::uint32_t, std::uint64_t) {},
				.on_session_reject = [name = std::string(line.desc.name)](
					std::uint32_t socket_index,
					std::uint64_t current_sessions,
					std::string_view reason) {
					spdlog::warn(
						"{} line reject. socket_index={}, current_sessions={}, reason={}",
						name,
						socket_index,
						current_sessions,
						reason);
				},
			},
			std::move(handler),
			dispatch))
		{
			spdlog::error("{} line start failed.", line.desc.name);
			return false;
		}

		return true;
	}

} // namespace dc
