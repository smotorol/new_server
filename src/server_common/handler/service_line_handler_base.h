#pragma once

#include <cstdint>

#include "app/runtime/networkex_base.h"

namespace dc {

	enum class ServiceLineId : std::uint32_t
	{
		World = 0,
		Login = 1,
		Control = 2,
	};

	class ServiceLineHandlerBase : public NetworkEXBase
	{
	public:
		using NetworkEXBase::NetworkEXBase;
		~ServiceLineHandlerBase() override = default;

	protected:
		bool DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex,
			_MSG_HEADER* pMsgHeader, char* pMsg) final
		{
			if (!pMsgHeader) return false;

			switch (static_cast<ServiceLineId>(dwProID))
			{
			case ServiceLineId::World:
				return HandleWorldPacket(dwProID, dwClientIndex, pMsgHeader, pMsg);
			case ServiceLineId::Login:
				return HandleLoginPacket(dwProID, dwClientIndex, pMsgHeader, pMsg);
			case ServiceLineId::Control:
				return HandleControlPacket(dwProID, dwClientIndex, pMsgHeader, pMsg);
			default:
				return HandleUnknownLinePacket(dwProID, dwClientIndex, pMsgHeader, pMsg);
			}
		}

		void AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex,
			std::uint32_t dwSerial) final
		{
			switch (static_cast<ServiceLineId>(dwProID))
			{
			case ServiceLineId::World:
				OnWorldAccepted(dwIndex, dwSerial);
				return;
			case ServiceLineId::Login:
				OnLoginAccepted(dwIndex, dwSerial);
				return;
			case ServiceLineId::Control:
				OnControlAccepted(dwIndex, dwSerial);
				return;
			default:
				OnUnknownLineAccepted(dwProID, dwIndex, dwSerial);
				return;
			}
		}

		void CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex,
			std::uint32_t dwSerial) final
		{
			switch (static_cast<ServiceLineId>(dwProID))
			{
			case ServiceLineId::World:
				OnWorldDisconnected(dwIndex, dwSerial);
				return;
			case ServiceLineId::Login:
				OnLoginDisconnected(dwIndex, dwSerial);
				return;
			case ServiceLineId::Control:
				OnControlDisconnected(dwIndex, dwSerial);
				return;
			default:
				OnUnknownLineDisconnected(dwProID, dwIndex, dwSerial);
				return;
			}
		}

		virtual bool HandleWorldPacket(std::uint32_t dwProID, std::uint32_t n,
			_MSG_HEADER* pMsgHeader, char* pMsg)
		{
			(void)dwProID; (void)n; (void)pMsgHeader; (void)pMsg;
			return false;
		}

		virtual bool HandleLoginPacket(std::uint32_t dwProID, std::uint32_t n,
			_MSG_HEADER* pMsgHeader, char* pMsg)
		{
			(void)dwProID; (void)n; (void)pMsgHeader; (void)pMsg;
			return false;
		}

		virtual bool HandleControlPacket(std::uint32_t dwProID, std::uint32_t n,
			_MSG_HEADER* pMsgHeader, char* pMsg)
		{
			(void)dwProID; (void)n; (void)pMsgHeader; (void)pMsg;
			return false;
		}

		virtual bool HandleUnknownLinePacket(std::uint32_t dwProID, std::uint32_t n,
			_MSG_HEADER* pMsgHeader, char* pMsg)
		{
			(void)dwProID; (void)n; (void)pMsgHeader; (void)pMsg;
			return false;
		}

		virtual void OnWorldAccepted(std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwIndex; (void)dwSerial;
		}

		virtual void OnLoginAccepted(std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwIndex; (void)dwSerial;
		}

		virtual void OnControlAccepted(std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwIndex; (void)dwSerial;
		}

		virtual void OnUnknownLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwProID; (void)dwIndex; (void)dwSerial;
		}

		virtual void OnWorldDisconnected(std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwIndex; (void)dwSerial;
		}

		virtual void OnLoginDisconnected(std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwIndex; (void)dwSerial;
		}

		virtual void OnControlDisconnected(std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwIndex; (void)dwSerial;
		}

		virtual void OnUnknownLineDisconnected(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
		{
			(void)dwProID; (void)dwIndex; (void)dwSerial;
		}
	};

} // namespace dc
