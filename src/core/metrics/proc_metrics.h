#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace procmetrics {

	struct ProcSnapshot {
		bool valid = false;
		long long total_cpu_ticks = 0;
		std::uint64_t rss_bytes = 0;
		std::uint32_t logical_cpu_count = 0;
	};

	inline ProcSnapshot ReadSelfSnapshot()
	{
		ProcSnapshot s{};
#if defined(__linux__)
		std::ifstream statf("/proc/self/stat");
		if (!statf) return s;
		std::string line;
		std::getline(statf, line);
		const auto rparen = line.rfind(')');
		if (rparen == std::string::npos || rparen + 2 >= line.size()) return s;
		std::istringstream iss(line.substr(rparen + 2));

		std::string state;
		iss >> state;

		long long skip_ll = 0;
		unsigned long long skip_ull = 0;
		for (int i = 0; i < 10; ++i) iss >> skip_ll; // ppid..cmajflt

		long long utime = 0;
		long long stime = 0;
		iss >> utime >> stime;

		for (int i = 0; i < 7; ++i) iss >> skip_ll; // cutime..itrealvalue

		long long starttime = 0;
		long long vsize = 0;
		long long rss_pages = 0;
		iss >> starttime >> vsize >> rss_pages;
		(void)starttime;
		(void)vsize;

		const long page_size = sysconf(_SC_PAGESIZE);
		s.valid = true;
		s.total_cpu_ticks = utime + stime;
		s.rss_bytes = (rss_pages > 0 && page_size > 0) ? static_cast<std::uint64_t>(rss_pages) * static_cast<std::uint64_t>(page_size) : 0;
		s.logical_cpu_count = std::max(1u, std::thread::hardware_concurrency());
#else
#endif
		return s;
	}

	inline double CpuPercentBetween(const ProcSnapshot& a,
		const ProcSnapshot& b,
		std::chrono::steady_clock::duration wall)
	{
#if defined(__linux__)
		if (!a.valid || !b.valid) return 0.0;
		const long ticks_per_sec = sysconf(_SC_CLK_TCK);
		if (ticks_per_sec <= 0) return 0.0;
		const double wall_sec = std::chrono::duration<double>(wall).count();
		if (wall_sec <= 0.0) return 0.0;
		const long long delta_ticks = b.total_cpu_ticks - a.total_cpu_ticks;
		if (delta_ticks <= 0) return 0.0;
		return (static_cast<double>(delta_ticks) / static_cast<double>(ticks_per_sec)) * 100.0 / wall_sec;
#else
		(void)a; (void)b; (void)wall;
		return 0.0;
#endif
	}

	inline std::uint64_t BytesToMiB(std::uint64_t bytes)
	{
		return bytes / (1024ull * 1024ull);
	}

} // namespace procmetrics
