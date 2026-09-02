#include "pch.h"
#include "indicators.h"
#include "indicators/internal.h"
#include "session.h"

#include <sstream>
#include <unordered_map>
#include <vector>

namespace cmp_indicators {

std::size_t g_compassPeerSlots{ 0 };
std::vector<RE::ObjectRefHandle> g_mapMarkerHandles;
std::unordered_map<std::uint32_t, RE::ObjectRefHandle> g_peerMarkerRefs;
int g_compassPlayerSetFrame{ 0 };
int g_compassQuestFrame{ 0 };
bool g_loggedCompassPath{ false };
bool g_loggedCompassFail{ false };
bool g_lastHudRootOk{ false };
bool g_lastCompassOk{ false };
std::size_t g_lastCompassPeers{ 0 };
std::size_t g_lastMapMarkers{ 0 };

}  // namespace cmp_indicators

void CMP_IndicatorsClear()
{
	cmp_indicators::ClearCompassPeers();
	cmp_indicators::ClearMapMarkers();
}

void CMP_IndicatorsTick()
{
	auto& s = CMP_Session();
	if (!s.settings.pointerHud || !s.net.joined) {
		CMP_IndicatorsClear();
		return;
	}

	const auto peers = cmp_indicators::CollectPeers();
	if (peers.empty()) {
		CMP_IndicatorsClear();
		return;
	}

	cmp_indicators::UpdateCompass(peers);
	cmp_indicators::UpdateMapMarkers(peers);
}

std::string CMP_IndicatorsDebugText()
{
	auto& s = CMP_Session();
	std::ostringstream o;
	o << "pointerHud=" << (s.settings.pointerHud ? 1 : 0)
	  << " joined=" << (s.net.joined ? 1 : 0)
	  << " hudRoot=" << (cmp_indicators::g_lastHudRootOk ? 1 : 0)
	  << " compass=" << (cmp_indicators::g_lastCompassOk ? 1 : 0)
	  << " compassPeers=" << cmp_indicators::g_lastCompassPeers
	  << " mapMarkers=" << cmp_indicators::g_lastMapMarkers;
	return o.str();
}

void CMP_IndicatorsRefreshMap()
{
	if (auto* ui = RE::UI::GetSingleton()) {
		const auto menu = ui->GetMenu<RE::PipboyMenu>();
		if (menu) {
			menu->RefreshMapMarkers(0);
		}
	}
}
