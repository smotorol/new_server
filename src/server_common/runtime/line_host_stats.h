#pragma once

#include <atomic>
#include <cstdint>

namespace dc {

	struct LineHostStats {
		std::atomic<std::uint64_t> start_count{ 0 };
		std::atomic<std::uint64_t> stop_count{ 0 };

		std::atomic<std::uint64_t> session_open_count{ 0 };
		std::atomic<std::uint64_t> session_close_count{ 0 };

		std::atomic<std::uint64_t> session_reject_count{ 0 };
		std::atomic<std::uint64_t> session_reject_by_limit_count{ 0 };
		std::atomic<std::uint64_t> session_reject_by_policy_count{ 0 };

		std::atomic<std::uint64_t> current_sessions{ 0 };
		std::atomic<std::uint64_t> peak_sessions{ 0 };

		void OnStart() noexcept
		{
			start_count.fetch_add(1, std::memory_order_relaxed);
		}

		void OnStop() noexcept
		{
			stop_count.fetch_add(1, std::memory_order_relaxed);
		}

		void OnSessionOpen() noexcept
		{
			session_open_count.fetch_add(1, std::memory_order_relaxed);

			const auto cur = current_sessions.fetch_add(1, std::memory_order_relaxed) + 1;

			auto prev_peak = peak_sessions.load(std::memory_order_relaxed);
			while (cur > prev_peak &&
				!peak_sessions.compare_exchange_weak(prev_peak, cur, std::memory_order_relaxed))
			{
			}
		}

		void OnSessionClose() noexcept
		{
			session_close_count.fetch_add(1, std::memory_order_relaxed);

			auto cur = current_sessions.load(std::memory_order_relaxed);
			while (cur > 0 &&
				!current_sessions.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed))
			{
			}
		}

		void OnSessionRejectByLimit() noexcept
		{
			session_reject_count.fetch_add(1, std::memory_order_relaxed);
			session_reject_by_limit_count.fetch_add(1, std::memory_order_relaxed);
		}

		void OnSessionRejectByPolicy() noexcept
		{
			session_reject_count.fetch_add(1, std::memory_order_relaxed);
			session_reject_by_policy_count.fetch_add(1, std::memory_order_relaxed);
		}

		std::uint64_t CurrentSessions() const noexcept
		{
			return current_sessions.load(std::memory_order_relaxed);
		}

		std::uint64_t PeakSessions() const noexcept
		{
			return peak_sessions.load(std::memory_order_relaxed);
		}
	};

} // namespace dc
