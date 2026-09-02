#include "persist_worker.hpp"

#include <chrono>
#include <thread>
#include <unordered_set>

#include "log.hpp"
#include "server_state.hpp"

PersistWorker::PersistWorker() = default;

PersistWorker::~PersistWorker()
{
	stop();
}

void PersistWorker::start()
{
	if (running_.exchange(true)) {
		return;
	}
	thread_ = std::thread([this] { loop(); });
}

void PersistWorker::stop()
{
	if (!running_.exchange(false)) {
		return;
	}
	queue_.push(PersistJob{});
	if (thread_.joinable()) {
		thread_.join();
	}
}

bool PersistWorker::enqueue(PersistJob job)
{
	return queue_.push(std::move(job));
}

void PersistWorker::enqueue_blocking(PersistJob job)
{
	while (!queue_.push(job) && running_.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void PersistWorker::loop()
{
	while (running_.load(std::memory_order_relaxed)) {
		PersistJob job;
		if (!queue_.wait_pop(job, [this] { return !running_.load(std::memory_order_relaxed); }, 50)) {
			continue;
		}
		if (!running_.load(std::memory_order_relaxed) && job.players.empty() && !job.world && !job.writeBans) {
			break;
		}
		if (job.world) {
			persist_world(job.worldSnap);
		}
		for (const auto& rec : job.players) {
			persist_player(rec);
		}
		if (job.writeBans) {
			std::unordered_set<std::string> bans(job.banKeys.begin(), job.banKeys.end());
			persist_bans(bans);
		}
	}
	// Drain remaining jobs on shutdown.
	PersistJob job;
	while (queue_.try_pop(job)) {
		if (job.world) {
			persist_world(job.worldSnap);
		}
		for (const auto& rec : job.players) {
			persist_player(rec);
		}
		if (job.writeBans) {
			std::unordered_set<std::string> bans(job.banKeys.begin(), job.banKeys.end());
			persist_bans(bans);
		}
	}
	LOG_INFO("persist worker stopped");
}
