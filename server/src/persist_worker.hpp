#pragma once

#include <atomic>
#include <thread>

#include "queues.hpp"

class PersistWorker {
public:
	PersistWorker();
	~PersistWorker();

	PersistWorker(const PersistWorker&) = delete;
	PersistWorker& operator=(const PersistWorker&) = delete;

	void start();
	void stop();
	bool enqueue(PersistJob job);
	void enqueue_blocking(PersistJob job);

private:
	void loop();

	AsyncQueue<PersistJob> queue_{ 256 };
	std::atomic<bool> running_{ false };
	std::thread thread_;
};
