#pragma once

#include "server_common/runtime/line_descriptor.h"
#include "server_common/runtime/line_host.h"

namespace dc {

	struct HostedLineEntry
	{
		LineDescriptor desc{};
		LineHost host{};
	};

	template <typename EnumT, std::size_t N>
	class BasicLineRegistry
	{
	public:
		using enum_type = EnumT;
		using storage_type = std::array<HostedLineEntry, N>;

	public:
		BasicLineRegistry() = default;

		explicit BasicLineRegistry(storage_type entries)
			: lines_(std::move(entries))
		{
		}

		static constexpr std::size_t ToIndex(EnumT id) noexcept
		{
			return static_cast<std::size_t>(id);
		}

		HostedLineEntry& entry(EnumT id) noexcept
		{
			return lines_[ToIndex(id)];
		}

		const HostedLineEntry& entry(EnumT id) const noexcept
		{
			return lines_[ToIndex(id)];
		}

		LineHost& host(EnumT id) noexcept
		{
			return entry(id).host;
		}

		const LineHost& host(EnumT id) const noexcept
		{
			return entry(id).host;
		}

		LineDescriptor& desc(EnumT id) noexcept
		{
			return entry(id).desc;
		}

		const LineDescriptor& desc(EnumT id) const noexcept
		{
			return entry(id).desc;
		}

		storage_type& entries() noexcept
		{
			return lines_;
		}

		const storage_type& entries() const noexcept
		{
			return lines_;
		}

		void stop_all_reverse() noexcept
		{
			for (std::size_t i = lines_.size(); i-- > 0;) {
				lines_[i].host.Stop();
			}
		}

	private:
		storage_type lines_{};
	};

} // namespace dc
