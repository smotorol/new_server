#include "services/account/runtime/account_line_runtime.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "server_common/runtime/line_start_helper.h"
#include "services/account/db/account_auth_db_repository.h"

namespace dc {

    AccountLineRuntime::AccountLineRuntime(std::uint16_t port)
        : port_(port)
    {
    }

    void AccountLineRuntime::MarkLoginRegistered(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint32_t server_id,
        std::string_view server_name,
        std::uint16_t listen_port)
    {
        login_sid_.store(sid, std::memory_order_relaxed);
        login_serial_.store(serial, std::memory_order_relaxed);
        login_ready_.store(true, std::memory_order_release);

        spdlog::info(
            "AccountLineRuntime login ready sid={} serial={} server_id={} server_name={} listen_port={}",
            sid, serial, server_id, server_name, listen_port);

        if (login_handler_) {
            login_handler_->SendRegisterAck(0, sid, serial, 1, 10, "account", port_);
        }
    }

    void AccountLineRuntime::MarkLoginDisconnected(
        std::uint32_t sid,
        std::uint32_t serial)
    {
        const auto cur_sid = login_sid_.load(std::memory_order_relaxed);
        const auto cur_serial = login_serial_.load(std::memory_order_relaxed);

        if (cur_sid != sid || cur_serial != serial) {
            return;
        }

        login_ready_.store(false, std::memory_order_release);
        login_sid_.store(0, std::memory_order_relaxed);
        login_serial_.store(0, std::memory_order_relaxed);
    }

    bool AccountLineRuntime::InitDqs_()
    {
        std::lock_guard lk(dqs_mtx_);

        dqs_slots_.assign(kMaxDqsSlotCount, {});
        dqs_empty_.clear();
        for (std::uint32_t i = 0; i < kMaxDqsSlotCount; ++i) {
            dqs_empty_.push_back(i);
        }

        dqs_push_count_ = 0;
        dqs_drop_count_ = 0;
        return true;
    }

    std::uint32_t AccountLineRuntime::RouteAccountShard_(
        const svr::dqs_payload::AccountAuthRequest& payload) const
    {
        if (db_shard_count_ <= 1) {
            return 0;
        }

        const std::string_view login_id(payload.login_id);
        std::uint64_t h = 1469598103934665603ull;
        for (char c : login_id) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }

        return static_cast<std::uint32_t>(h % db_shard_count_);
    }

    bool AccountLineRuntime::PushAccountAuthDqs_(
        const svr::dqs_payload::AccountAuthRequest& payload)
    {
        static_assert(sizeof(payload) <= svr::dqs::DqsSlot::max_data_size);

        std::uint32_t idx = 0;
        {
            std::lock_guard lk(dqs_mtx_);
            if (dqs_empty_.empty()) {
                ++dqs_drop_count_;
                spdlog::warn("AccountLineRuntime DQS drop: no empty slot.");
                return false;
            }

            idx = dqs_empty_.front();
            dqs_empty_.pop_front();

            auto& slot = dqs_slots_[idx];
            slot.reset();
            slot.process_code = static_cast<std::uint8_t>(svr::dqs::ProcessCode::account);
            slot.qry_case = static_cast<std::uint8_t>(svr::dqs::QueryCase::account_auth);
            slot.result = svr::dqs::ResultCode::success;
            slot.in_use = true;
            slot.done = false;
            slot.data_size = static_cast<std::uint16_t>(sizeof(payload));
            std::memcpy(slot.data.data(), &payload, sizeof(payload));
        }

        const auto shard = RouteAccountShard_(payload);
        if (!db_shards_) {
            RecycleDqsSlot_(idx);
            ++dqs_drop_count_;
            spdlog::error("AccountLineRuntime DQS drop: db_shards_ is null.");
            return false;
        }

        ++dqs_push_count_;
        db_shards_->push(shard, idx);
        return true;
    }

    bool AccountLineRuntime::InitDbWorkers_()
    {
        db_worker_conns_.clear();
        db_worker_conns_.reserve(db_shard_count_);

        try {
            for (std::uint32_t i = 0; i < db_shard_count_; ++i) {
                auto conn = std::make_unique<db::OdbcConnection>();
                if (!conn->Connect(db_conn_str_)) {
                    spdlog::error("AccountLineRuntime DB connect failed. shard={}", i);
                    return false;
                }
                db_worker_conns_.push_back(std::move(conn));
            }
        }
        catch (const std::exception& e) {
            spdlog::error("AccountLineRuntime DB worker init exception: {}", e.what());
            return false;
        }

        db_shards_ = std::make_unique<svr::dbshard::DbShardManager>(
            db_shard_count_,
            [this](std::uint32_t slot_index) {
            OnDqsRunOne_(slot_index);
        },
            [this](std::uint32_t slot_index) {
            RecycleDqsSlot_(slot_index);
        });

        db_shards_->start();

        spdlog::info(
            "AccountLineRuntime DB workers started. shard_count={}",
            db_shard_count_);

        return true;
    }

    void AccountLineRuntime::ShutdownDbWorkers_()
    {
        if (db_shards_) {
            db_shards_->stop();
            db_shards_.reset();
        }

        db_worker_conns_.clear();
    }

    void AccountLineRuntime::OnDqsRunOne_(std::uint32_t slot_index)
    {
        if (slot_index >= dqs_slots_.size()) {
            return;
        }

        auto& slot = dqs_slots_[slot_index];
        const auto pc = static_cast<svr::dqs::ProcessCode>(slot.process_code);
        const auto qc = static_cast<svr::dqs::QueryCase>(slot.qry_case);

        if (pc != svr::dqs::ProcessCode::account ||
            qc != svr::dqs::QueryCase::account_auth)
        {
            slot.result = svr::dqs::ResultCode::invalid_data;
            return;
        }

        if (slot.data_size < sizeof(svr::dqs_payload::AccountAuthRequest)) {
            slot.result = svr::dqs::ResultCode::invalid_data;
            return;
        }

        svr::dqs_payload::AccountAuthRequest payload{};
        std::memcpy(&payload, slot.data.data(), sizeof(payload));

        svr::dqs_result::AccountAuthResult rr{};
        rr.sid = payload.sid;
        rr.serial = payload.serial;
        rr.request_id = payload.request_id;
        rr.ok = 0;
        rr.account_id = 0;
        rr.char_id = 0;
        rr.result = svr::dqs::ResultCode::success;

        try {
            const auto shard = RouteAccountShard_(payload);
            if (shard >= db_worker_conns_.size() || !db_worker_conns_[shard]) {
                rr.result = svr::dqs::ResultCode::db_error;
                std::snprintf(rr.fail_reason, sizeof(rr.fail_reason), "%s", "db_conn_not_ready");
                PostDqsResult_(rr);
                return;
            }

            dc::account::AccountAuthRequestJob job{};
            job.sid = payload.sid;
            job.serial = payload.serial;
            job.request_id = payload.request_id;
            job.selected_char_id = payload.selected_char_id;
            job.login_id = payload.login_id;
            job.password = payload.password;

            const auto db_result =
                dc::account::AccountAuthDbRepository::ExecuteAuth(
                    *db_worker_conns_[shard],
                    job);

            rr.ok = db_result.ok ? 1 : 0;
            rr.account_id = db_result.account_id;
            rr.char_id = db_result.char_id;

            std::snprintf(
                rr.fail_reason,
                sizeof(rr.fail_reason),
                "%.*s",
                static_cast<int>(db_result.fail_reason.size()),
                db_result.fail_reason.c_str());

            PostDqsResult_(rr);
        }
        catch (const std::exception& e) {
            rr.result = svr::dqs::ResultCode::db_error;
            std::snprintf(
                rr.fail_reason,
                sizeof(rr.fail_reason),
                "%.*s",
                static_cast<int>(std::strlen(e.what())),
                e.what());
            PostDqsResult_(rr);
        }
        catch (...) {
            rr.result = svr::dqs::ResultCode::db_error;
            std::snprintf(rr.fail_reason, sizeof(rr.fail_reason), "%s", "unknown_db_exception");
            PostDqsResult_(rr);
        }
    }

    void AccountLineRuntime::RecycleDqsSlot_(std::uint32_t slot_index)
    {
        std::lock_guard lk(dqs_mtx_);

        if (slot_index >= dqs_slots_.size()) {
            return;
        }

        dqs_slots_[slot_index].reset();
        dqs_empty_.push_back(slot_index);
    }

    void AccountLineRuntime::PostDqsResult_(svr::dqs_result::Result result)
    {
        std::lock_guard lk(dqs_result_mtx_);
        dqs_results_.push_back(std::move(result));
    }

    void AccountLineRuntime::HandleDqsResult_(const svr::dqs_result::AccountAuthResult& rr)
    {
        if (!login_handler_) {
            return;
        }

        const bool ok =
            (rr.ok != 0) &&
            (rr.result == svr::dqs::ResultCode::success);

        login_handler_->SendAccountAuthResult(
            0,
            rr.sid,
            rr.serial,
            rr.request_id,
            ok,
            rr.account_id,
            rr.char_id,
            rr.fail_reason);

        if (ok) {
            spdlog::info(
                "Account auth success request_id={} account_id={} char_id={} sid={} serial={}",
                rr.request_id,
                rr.account_id,
                rr.char_id,
                rr.sid,
                rr.serial);
        }
        else {
            spdlog::warn(
                "Account auth failed request_id={} sid={} serial={} reason={} result={}",
                rr.request_id,
                rr.sid,
                rr.serial,
                rr.fail_reason,
                static_cast<int>(rr.result));
        }
    }

    void AccountLineRuntime::DrainDqsResults_()
    {
        std::deque<svr::dqs_result::Result> local;
        {
            std::lock_guard lk(dqs_result_mtx_);
            local.swap(dqs_results_);
        }

        for (auto& r : local) {
            std::visit([this](auto& rr) {
                using T = std::decay_t<decltype(rr)>;
                if constexpr (std::is_same_v<T, svr::dqs_result::AccountAuthResult>) {
                    HandleDqsResult_(rr);
                }
            }, r);
        }
    }

    void AccountLineRuntime::HandleAccountAuthRequest(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t request_id,
        std::string_view login_id,
        std::string_view password,
        std::uint64_t selected_char_id)
    {
        if (!login_handler_) {
            return;
        }

        if (login_id.empty()) {
            login_handler_->SendAccountAuthResult(
                0, sid, serial, request_id,
                false, 0, 0, "empty_login_id");
            return;
        }

        if (password.empty()) {
            login_handler_->SendAccountAuthResult(
                0, sid, serial, request_id,
                false, 0, 0, "empty_password");
            return;
        }

        svr::dqs_payload::AccountAuthRequest payload{};
        payload.sid = sid;
        payload.serial = serial;
        payload.request_id = request_id;
        payload.selected_char_id = selected_char_id;

        std::snprintf(
            payload.login_id,
            sizeof(payload.login_id),
            "%.*s",
            static_cast<int>(login_id.size()),
            login_id.data());

        std::snprintf(
            payload.password,
            sizeof(payload.password),
            "%.*s",
            static_cast<int>(password.size()),
            password.data());

        if (!PushAccountAuthDqs_(payload)) {
            login_handler_->SendAccountAuthResult(
                0, sid, serial, request_id,
                false, 0, 0, "db_queue_full");
            return;
        }
    }

    bool AccountLineRuntime::OnRuntimeInit()
    {
        if (!InitDqs_()) {
            spdlog::error("AccountLineRuntime failed to init DQS.");
            return false;
        }

        if (!InitDbWorkers_()) {
            spdlog::error("AccountLineRuntime failed to init DB workers.");
            return false;
        }

        dc::InitHostedLineEntry(
            login_line_,
            0,
            "account-login",
            port_,
            false,
            0);

        login_handler_ = std::make_shared<AccountLoginHandler>(
            [this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port) {
            MarkLoginRegistered(sid, serial, server_id, server_name, listen_port);
        },
            [this](std::uint32_t sid, std::uint32_t serial) {
            MarkLoginDisconnected(sid, serial);
        },
            [this](std::uint32_t sid, std::uint32_t serial, std::uint64_t request_id, std::string_view login_id, std::string_view password, std::uint64_t selected_char_id) {
            HandleAccountAuthRequest(sid, serial, request_id, login_id, password, selected_char_id);
        });

        if (!dc::StartHostedLine(
            login_line_,
            io_,
            login_handler_,
            [](std::uint64_t, std::function<void()> fn) {
            if (fn) fn();
        }))
        {
            spdlog::error("AccountLineRuntime failed to start hosted line. port={}", port_);
            return false;
        }

        spdlog::info("AccountLineRuntime started. login_port={}", port_);
        return true;
    }

    void AccountLineRuntime::OnBeforeIoStop()
    {
        login_ready_.store(false, std::memory_order_release);
    }

    void AccountLineRuntime::OnAfterIoStop()
    {
        login_line_.host.Stop();
        login_handler_.reset();

        login_ready_.store(false, std::memory_order_release);
        login_sid_.store(0, std::memory_order_relaxed);
        login_serial_.store(0, std::memory_order_relaxed);

        ShutdownDbWorkers_();
    }

    void AccountLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point)
    {
        DrainDqsResults_();
    }

} // namespace dc
