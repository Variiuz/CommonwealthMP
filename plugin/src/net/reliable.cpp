#include "pch.h"
#include "session.h"
#include "net.h"
#include "udp_win.h"
#include "cmp_reliable.hpp"
#include "net/internal.h"

namespace {

cmp::ReliableChannel g_reliable;

void SendRaw(const void* data, int len)
{
	auto& s = CMP_Session();
	cmp_udp_send(s.settings.host.c_str(), s.settings.port, data, len);
}

}  // namespace

void CMP_Reliable_Reset()
{
	g_reliable.clear();
	auto& s = CMP_Session();
	g_reliable.set_max_tries(s.settings.reliableRetries);
}

void CMP_Reliable_Send(const void* data, int len)
{
	if (!data || len < static_cast<int>(sizeof(cmp::Header))) {
		return;
	}
	std::vector<std::uint8_t> copy(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + len);
	auto* h = reinterpret_cast<cmp::Header*>(copy.data());
	const auto type = static_cast<cmp::Msg>(h->type);
	if (!cmp::msg_is_reliable(type)) {
		SendRaw(copy.data(), len);
		return;
	}
	const double now = cmp_net::NowSec();
	g_reliable.set_max_tries(CMP_Session().settings.reliableRetries);
	const auto seq = g_reliable.stamp(copy.data(), len);
	g_reliable.track(seq, copy.data(), len, now);
	SendRaw(copy.data(), len);
}

void CMP_Reliable_Tick()
{
	auto& s = CMP_Session();
	if (!s.net.joined) {
		return;
	}
	g_reliable.tick(cmp_net::NowSec(), [](const void* data, int len) { SendRaw(data, len); });
}

bool CMP_Reliable_HandleAck(const cmp::Ack& ack)
{
	const double now = cmp_net::NowSec();
	if (auto it = g_reliable.pending.find(ack.ackSeq); it != g_reliable.pending.end()) {
		const float rtt = static_cast<float>((now - it->second.lastSendSec) * 1000.0);
		auto& s = CMP_Session();
		if (s.net.measuredRttMs <= 0.0f) {
			s.net.measuredRttMs = rtt;
		} else {
			s.net.measuredRttMs = s.net.measuredRttMs * 0.8f + rtt * 0.2f;
		}
	}
	return g_reliable.on_ack(ack.ackSeq, now);
}

bool CMP_Reliable_AlreadyHandled(std::uint16_t seq)
{
	if (seq == 0) {
		return false;
	}
	return g_reliable.already_handled(seq, cmp_net::NowSec());
}

void CMP_Reliable_MaybeAck(const cmp::Header& header)
{
	if ((header.flags & cmp::HeaderFlag::Reliable) == 0 || header.seq == 0) {
		return;
	}
	auto& s = CMP_Session();
	const auto ack = cmp::make_ack(header.seq, s.net.myPeerId);
	SendRaw(&ack, static_cast<int>(sizeof(ack)));
}

float CMP_EffectiveInterpDelayMs()
{
	auto& s = CMP_Session();
	if (s.settings.interpDelayMs == 0) {
		return 0.0f;
	}
	if (s.settings.interpDelayMs > 0) {
		return static_cast<float>(s.settings.interpDelayMs);
	}
	const float tickMs = 1000.0f / static_cast<float>(std::max(1, s.settings.poseHz));
	float delay = tickMs * 1.5f;
	if (s.net.measuredRttMs > 0.0f) {
		delay = std::max(delay, s.net.measuredRttMs * 0.5f + tickMs);
	}
	return std::clamp(delay, 20.0f, 250.0f);
}
