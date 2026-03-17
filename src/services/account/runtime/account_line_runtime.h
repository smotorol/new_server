#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "server_common/runtime/line_registry.h"
#include "services/account/handler/account_login_handler.h"
#include "services/runtime/server_runtime_base.h"

#include "db/core/dqs_payloads.h"
#include "db/core/dqs_results.h"
#include "db/core/dqs_types.h"
#include "db/odbc/odbc_wrapper.h"
#include "db/shard/db_shard_manager.h"
#include "services/account/handler/account_world_handler.h"

namespace dc {

    class AccountLineRuntime final : public ServerRuntimeBase {
    public:
        explicit AccountLineRuntime(std::uint16_t login_port, std::uint16_t world_port);
        ~AccountLineRuntime() override = default;

    private:
        static constexpr std::uint32_t kMaxDqsSlotCount = 1024;

    private:
        struct PendingWorldTicketUpsert
        {
            std::uint64_t request_id = 0;
            std::uint32_t login_sid = 0;
            std::uint32_t login_serial = 0;

            std::uint64_t account_id = 0;
            std::uint64_t char_id = 0;

            std::string login_session;
            std::string world_token;
            std::string world_host;
            std::uint16_t world_port = 0;

            std::chrono::steady_clock::time_point issued_at{};
        };

    private:
        bool OnRuntimeInit() override;
        void OnBeforeIoStop() override;
        void OnAfterIoStop() override;
        void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

        void MarkLoginRegistered(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint32_t server_id,
            std::string_view server_name,
            std::uint16_t listen_port);

        void MarkLoginDisconnected(
            std::uint32_t sid,
            std::uint32_t serial);

        void HandleAccountAuthRequest(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint64_t request_id,
            std::string_view login_id,
            std::string_view password,
            std::uint64_t selected_char_id);

        void MarkWorldRegistered(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint32_t server_id,
            std::string_view server_name,
            std::string_view public_host,
            std::uint16_t public_port);

        void MarkWorldDisconnected(
            std::uint32_t sid,
            std::uint32_t serial);

        bool TryGetWorldEndpoint_(
            std::string& out_host,
            std::uint16_t& out_port) const;

        std::string GenerateWorldToken_() const;

        void HandleWorldAuthTicketConsume(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint64_t request_id,
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string_view login_session,
            std::string_view world_token);

        std::uint16_t ConsumeWorldAuthTicketBrokered_(
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string_view login_session,
            std::string_view world_token,
            std::uint64_t& out_account_id,
            std::uint64_t& out_char_id,
            std::string& out_login_session,
            std::string& out_world_token);

        void OnWorldEnterSuccessNotify(
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string_view login_session,
            std::string_view world_token);

        void ExpirePendingWorldTickets_(std::chrono::steady_clock::time_point now);
    private:
        bool InitDbWorkers_();
        void ShutdownDbWorkers_();

        bool InitDqs_();
        std::uint32_t RouteAccountShard_(const svr::dqs_payload::AccountAuthRequest& payload) const;
        bool PushAccountAuthDqs_(const svr::dqs_payload::AccountAuthRequest& payload);

        void OnDqsRunOne_(std::uint32_t slot_index);
        void RecycleDqsSlot_(std::uint32_t slot_index);

        void PostDqsResult_(svr::dqs_result::Result result);
        void DrainDqsResults_();
        void HandleDqsResult_(const svr::dqs_result::AccountAuthResult& rr);

    private:
        std::uint16_t login_port_ = 0;
        std::uint16_t world_port_ = 0;

        std::atomic<std::uint32_t> login_sid_{ 0 };
        std::atomic<std::uint32_t> login_serial_{ 0 };
        std::atomic<bool> login_ready_{ false };

        std::atomic<std::uint32_t> world_sid_{ 0 };
        std::atomic<std::uint32_t> world_serial_{ 0 };
        std::atomic<bool> world_ready_{ false };

        mutable std::mutex world_endpoint_mtx_;
        std::string world_public_host_ = "127.0.0.1";
        std::uint16_t world_public_port_ = 0;

        HostedLineEntry login_line_{};
        HostedLineEntry world_line_{};

        std::shared_ptr<AccountLoginHandler> login_handler_;
        std::shared_ptr<AccountWorldHandler> world_handler_;

        std::mutex pending_world_upsert_mtx_;
        std::unordered_map<std::string, PendingWorldTicketUpsert> pending_world_upserts_;

    private:
        std::string db_conn_str_ =
            "DRIVER={ODBC Driver 18 for SQL Server};"
            "SERVER=127.0.0.1,11433;"
            "UID=sa;"
            "PWD=Strong!Pass123;"
            "Encrypt=optional;"
            "DATABASE=NFX_AUTH;";

        std::uint32_t db_shard_count_ = 2;
        std::vector<std::unique_ptr<db::OdbcConnection>> db_worker_conns_;
        std::unique_ptr<svr::dbshard::DbShardManager> db_shards_;

    private:
        std::mutex dqs_mtx_;
        std::vector<svr::dqs::DqsSlot> dqs_slots_;
        std::deque<std::uint32_t> dqs_empty_;
        std::atomic<std::uint64_t> dqs_push_count_{ 0 };
        std::atomic<std::uint64_t> dqs_drop_count_{ 0 };

        std::mutex dqs_result_mtx_;
        std::deque<svr::dqs_result::Result> dqs_results_;
    };

} // namespace dc
