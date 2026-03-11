#pragma once

#include <cstdint>
#include <functional>

#include "app/runtime/networkex_base.h"

namespace dc {

	class ServiceLineHandlerBase : public NetworkEXBase
	{
	public:
		using SessionOpenHook = std::function<bool(std::uint32_t, std::uint32_t)>;
		using SessionCloseHook = std::function<void(std::uint32_t, std::uint32_t)>;

	public:
		ServiceLineHandlerBase() = default;
		~ServiceLineHandlerBase() override = default;

		void SetSessionLifecycleHooks(SessionOpenHook on_open, SessionCloseHook on_close)
		{
			on_session_open_ = std::move(on_open);
			on_session_close_ = std::move(on_close);
		}
	protected:

		void AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex,
			std::uint32_t dwSerial) final override
		{
			if (on_session_open_ && !on_session_open_(dwIndex, dwSerial)) {
				Close(dwProID, dwIndex, dwSerial, false);
				return;
			}

			OnLineAccepted(dwProID, dwIndex, dwSerial);
		}

		void CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex,
			std::uint32_t dwSerial) final override
		{
			if (!ShouldHandleClose(dwIndex, dwSerial)) {
				return;
			}

			if (on_session_close_) {
				on_session_close_(dwIndex, dwSerial);
			}

			OnLineClosed(dwProID, dwIndex, dwSerial);
		}

		virtual void OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex,
			std::uint32_t dwSerial)
		{
			(void)dwProID;
			(void)dwIndex;
			(void)dwSerial;
		}

		virtual void OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex,
			std::uint32_t dwSerial)
		{
			(void)dwProID;
			(void)dwIndex;
			(void)dwSerial;
		}

		virtual bool ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwIndex;
			(void)dwSerial;
			return true;
		}

	private:
		SessionOpenHook on_session_open_;
		SessionCloseHook on_session_close_;
	};

} // namespace dc
