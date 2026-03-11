#include "services/runtime/server_runtime_base.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>

namespace dc {

    ServerRuntimeBase::ServerRuntimeBase() = default;

    bool ServerRuntimeBase::InitMainThread()
    {
        if (running_.exchange(true)) {
            return true;
        }

        if (!OnRuntimeInit()) {
            running_ = false;
            return false;
        }

        work_guard_.emplace(boost::asio::make_work_guard(io_));

        io_threads_.clear();
        io_threads_.reserve(static_cast<std::size_t>(std::max(1, io_thread_count_)));
        for (int i = 0; i < std::max(1, io_thread_count_); ++i) {
            io_threads_.emplace_back([this](std::stop_token) { io_.run(); });
        }

        return true;
    }

    void ServerRuntimeBase::ReleaseMainThread()
    {
        if (!running_.exchange(false)) {
            return;
        }

        OnBeforeIoStop();

        io_.stop();
        work_guard_.reset();

        for (auto& t : io_threads_) {
            if (t.joinable()) {
                t.request_stop();
                t.join();
            }
        }
        io_threads_.clear();

        OnAfterIoStop();
    }

    void ServerRuntimeBase::MainLoop()
    {
        if (!running_) {
            spdlog::error("MainLoop called but InitMainThread failed.");
            return;
        }

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            OnMainLoopTick(std::chrono::steady_clock::now());
        }
    }

} // namespace dc
