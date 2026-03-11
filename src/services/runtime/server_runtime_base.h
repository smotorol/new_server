#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/executor_work_guard.hpp>

namespace dc {

    class ServerRuntimeBase {
    public:
        ServerRuntimeBase();
        virtual ~ServerRuntimeBase() = default;

        bool InitMainThread();
        void ReleaseMainThread();
        void MainLoop();

    protected:
        using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

        virtual bool OnRuntimeInit() = 0;
        virtual void OnBeforeIoStop() = 0;
        virtual void OnAfterIoStop() = 0;
        virtual void OnMainLoopTick(std::chrono::steady_clock::time_point now) = 0;

        boost::asio::io_context io_{ 1 };
        int io_thread_count_ = 1;
        std::atomic<bool> running_{ false };

    private:
        std::vector<std::jthread> io_threads_;
        std::optional<WorkGuard> work_guard_;
    };

} // namespace dc
