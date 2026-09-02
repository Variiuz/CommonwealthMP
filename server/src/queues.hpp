#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "cmp_protocol.hpp"
#include "cmp_udp.hpp"
#include "server_state.hpp"

template <class T>
class AsyncQueue {
public:
	explicit AsyncQueue(std::size_t maxSize = 4096)
		: maxSize_(maxSize)
	{
	}

	bool push(T item)
	{
		std::lock_guard lock(mu_);
		if (q_.size() >= maxSize_) {
			return false;
		}
		q_.push_back(std::move(item));
		cv_.notify_one();
		return true;
	}

	bool try_pop(T& out)
	{
		std::lock_guard lock(mu_);
		if (q_.empty()) {
			return false;
		}
		out = std::move(q_.front());
		q_.pop_front();
		return true;
	}

	template <class Pred>
	bool wait_pop(T& out, Pred&& stopPred, int timeoutMs = 5)
	{
		std::unique_lock lock(mu_);
		if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
				return !q_.empty() || stopPred();
			})) {
			return false;
		}
		if (q_.empty()) {
			return false;
		}
		out = std::move(q_.front());
		q_.pop_front();
		return true;
	}

	[[nodiscard]] std::size_t size() const
	{
		std::lock_guard lock(mu_);
		return q_.size();
	}

	void clear()
	{
		std::lock_guard lock(mu_);
		q_.clear();
	}

private:
	mutable std::mutex mu_;
	std::condition_variable cv_;
	std::deque<T> q_;
	std::size_t maxSize_;
};

enum class InboundKind : std::uint8_t {
	TcpAccept = 1,
	TcpFrame = 2,
	TcpClosed = 3,
	UdpDatagram = 4
};

struct InboundEvent {
	InboundKind kind{ InboundKind::TcpFrame };
	std::uint64_t connId{ 0 };
	std::uint32_t peerId{ 0 };
	sockaddr_in addr{};
	std::vector<std::uint8_t> bytes;
};

enum class OutboundKind : std::uint8_t {
	TcpSend = 1,
	UdpSend = 2,
	BindPeer = 3,
	CloseConn = 4,
	KeepPending = 5
};

struct OutboundCmd {
	OutboundKind kind{ OutboundKind::TcpSend };
	std::uint64_t connId{ 0 };
	std::uint32_t peerId{ 0 };
	sockaddr_in addr{};
	std::vector<std::uint8_t> bytes;
};

struct PersistJob {
	bool world{ false };
	SessionWorld worldSnap{};
	std::vector<PlayerRec> players;
	std::vector<std::string> banKeys;
	bool writeBans{ false };
};
