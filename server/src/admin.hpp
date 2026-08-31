#pragma once

#include <atomic>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class AdminConsole {
public:
	void start()
	{
		if (running_.exchange(true)) {
			return;
		}
		worker_ = std::thread([this] {
			std::string line;
			while (running_) {
				if (!std::getline(std::cin, line)) {
					break;
				}
				if (line.empty()) {
					continue;
				}
				std::lock_guard lock(mutex_);
				lines_.push(line);
			}
			running_ = false;
		});
	}

	void stop()
	{
		running_ = false;
		if (worker_.joinable()) {
			// stdin may block; detach so shutdown is not stuck on a closed console
			worker_.detach();
		}
	}

	bool poll(std::string& out)
	{
		std::lock_guard lock(mutex_);
		if (lines_.empty()) {
			return false;
		}
		out = std::move(lines_.front());
		lines_.pop();
		return true;
	}

private:
	std::atomic<bool> running_{ false };
	std::mutex mutex_;
	std::queue<std::string> lines_;
	std::thread worker_;
};
