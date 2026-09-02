#pragma once

#include "cmp_protocol.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

namespace cmp {

inline constexpr int kReliableMaxPending = 64;
inline constexpr int kReliableMaxTries = 8;
inline constexpr double kReliableResendSec = 0.25;

struct ReliablePending {
	std::uint16_t seq{ 0 };
	std::vector<std::uint8_t> payload;
	double lastSendSec{ 0.0 };
	int tries{ 0 };
};

struct ReliableChannel {
	std::uint16_t nextSeq{ 1 };
	std::unordered_map<std::uint16_t, ReliablePending> pending;
	std::unordered_map<std::uint16_t, double> handledInbound;

	void clear()
	{
		nextSeq = 1;
		pending.clear();
		handledInbound.clear();
	}

	std::uint16_t stamp(void* data, int len)
	{
		if (!data || len < static_cast<int>(sizeof(Header))) {
			return 0;
		}
		auto* h = static_cast<Header*>(data);
		const auto seq = nextSeq++;
		if (nextSeq == 0) {
			nextSeq = 1;
		}
		h->seq = seq;
		h->flags = static_cast<std::uint8_t>(h->flags | HeaderFlag::Reliable);
		return seq;
	}

	void track(std::uint16_t seq, const void* data, int len, double now)
	{
		if (seq == 0 || !data || len <= 0) {
			return;
		}
		while (static_cast<int>(pending.size()) >= kReliableMaxPending) {
			auto oldest = pending.begin();
			for (auto it = pending.begin(); it != pending.end(); ++it) {
				if (it->second.lastSendSec < oldest->second.lastSendSec) {
					oldest = it;
				}
			}
			pending.erase(oldest);
		}
		ReliablePending p;
		p.seq = seq;
		p.payload.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + len);
		p.lastSendSec = now;
		p.tries = 1;
		pending[seq] = std::move(p);
	}

	bool on_ack(std::uint16_t seq, double now)
	{
		(void)now;
		return pending.erase(seq) > 0;
	}

	bool already_handled(std::uint16_t seq, double now)
	{
		if (seq == 0) {
			return false;
		}
		if (auto it = handledInbound.find(seq); it != handledInbound.end()) {
			it->second = now;
			return true;
		}
		handledInbound[seq] = now;
		if (handledInbound.size() > 256) {
			const double cutoff = now - 30.0;
			for (auto it = handledInbound.begin(); it != handledInbound.end();) {
				if (it->second < cutoff) {
					it = handledInbound.erase(it);
				} else {
					++it;
				}
			}
		}
		return false;
	}

	template <class SendFn>
	void tick(double now, SendFn&& send)
	{
		std::vector<std::uint16_t> drop;
		for (auto& [seq, p] : pending) {
			if (now - p.lastSendSec < kReliableResendSec) {
				continue;
			}
			if (p.tries >= kReliableMaxTries) {
				drop.push_back(seq);
				continue;
			}
			send(p.payload.data(), static_cast<int>(p.payload.size()));
			p.lastSendSec = now;
			++p.tries;
		}
		for (const auto seq : drop) {
			pending.erase(seq);
		}
	}
};

}  // namespace cmp
