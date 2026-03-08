#include "endpoint_app.h"

namespace dc {

	EndpointApp::EndpointApp(EndpointConfig cfg, HandlerPtr handler)
		: cfg_(std::move(cfg))
		, handler_(std::move(handler))
	{}

	EndpointApp::~EndpointApp() {
		stop();

	}

	void EndpointApp::start()
	{
		if (running_.exchange(true)) return;

		work_.emplace(boost::asio::make_work_guard(io_));

		// ✅ dispatcher 자동 연결 (메인 단일-writer로 보냄)
		if (handler_) {
			handler_->AttachDispatcher([this](std::uint64_t /*actor_id*/,
				std::function<void()> fn) { post(std::move(fn)); });
		}

		if (cfg_.role == EndpointRole::Server) {
			// listen
			server_ = std::make_unique<net::TcpServer>(io_, cfg_.port, handler_);
			if (handler_) handler_->AttachServer(server_.get());

			boost::asio::co_spawn(io_,
				[this]() -> boost::asio::awaitable<void> {
					co_await server_->run();
				},
				boost::asio::detached);

		}
		else {
			// connect
			client_ = std::make_shared<net::TcpClient>(io_, handler_);
			if (handler_) handler_->AttachClient(client_);
			client_->start(cfg_.host, cfg_.port);

		}

		// io 스레드들
		const std::uint32_t n = (cfg_.net_threads == 0) ? 1 : cfg_.net_threads;
		net_threads_.reserve(n);
		for (std::uint32_t i = 0; i < n; ++i) {
			net_threads_.emplace_back([this] { io_.run(); });

		}

		if (cfg_.start_logic_thread) {
			start_logic_thread_();

		}
	}

	void EndpointApp::stop()
	{
		if (!running_.exchange(false)) return;

		// logic thread 종료
		q_cv_.notify_all();
		if (logic_thread_.joinable()) logic_thread_.join();

		// io 종료
		if (work_) work_.reset();
		io_.stop();
		for (auto& t : net_threads_) {
			if (t.joinable()) t.join();

		}
		net_threads_.clear();

		server_.reset();
		client_.reset();
	}

	void EndpointApp::post(std::function<void()> fn)
	{
		if (!fn) return;
		{
			std::lock_guard<std::mutex> lk(q_mtx_);
			q_.push_back(std::move(fn));
		}
		q_cv_.notify_one();
	}

	void EndpointApp::start_logic_thread_()
	{
		logic_thread_ = std::thread([this] { logic_loop_(); });
	}

	void EndpointApp::logic_loop_()
	{
		while (running_) {
			std::deque<std::function<void()>> local;
			{
				std::unique_lock<std::mutex> lk(q_mtx_);
				q_cv_.wait_for(lk, std::chrono::milliseconds(10), [&] {
					return !running_ || !q_.empty();
					});
				local.swap(q_);
			}
			for (auto& fn : local) {
				if (fn) fn();

			}

		}
	}


} // namespace dc
