#include "pch.h"
#include "session.h"
#include "net.h"
#include "udp_win.h"
#include "net/internal.h"

void CMP_Net_Send(const void* data, int len)
{
	if (!data || len < static_cast<int>(sizeof(cmp::Header))) {
		return;
	}
	auto& s = CMP_Session();
	const auto* h = static_cast<const cmp::Header*>(data);
	const auto type = static_cast<cmp::Msg>(h->type);
	if (cmp::msg_is_udp(type)) {
		cmp_net_udp_send(s.settings.host.c_str(), s.settings.port, data, len);
		return;
	}
	cmp_net_tcp_send(data, len);
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
