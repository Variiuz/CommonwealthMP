#include "pch.h"
#include "indicators/internal.h"

#include <vector>

namespace cmp_indicators {

void RemoveMapMarkerHandle(RE::PlayerCharacter* player, const RE::ObjectRefHandle& handle)
{
	if (!player || !handle) {
		return;
	}
	auto& arr = player->currentMapMarkers;
	for (auto it = arr.begin(); it != arr.end(); ++it) {
		if (*it == handle) {
			arr.erase(it);
			break;
		}
	}
}

void ClearMapMarkers()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	for (const auto& handle : g_mapMarkerHandles) {
		RemoveMapMarkerHandle(player, handle);
	}
	g_mapMarkerHandles.clear();
	g_peerMarkerRefs.clear();

	if (auto* ui = RE::UI::GetSingleton()) {
		const auto menu = ui->GetMenu<RE::PipboyMenu>();
		if (menu) {
			menu->RefreshMapMarkers(0);
		}
	}
}

void UpdateMapMarkers(const std::vector<PeerIndicator>& peers)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		g_lastMapMarkers = 0;
		return;
	}

	std::vector<RE::ObjectRefHandle> next;
	next.reserve(peers.size());
	for (const auto& peer : peers) {
		RE::ObjectRefHandle handle = peer.ghostHandle;
		if (handle) {
			if (const auto ptr = handle.get()) {
				if (auto* actor = ptr->As<RE::Actor>()) {
					actor->SetPosition(RE::NiPoint3{ peer.pose.x, peer.pose.y, peer.pose.z }, true);
				}
			}
		}
		if (!handle) {
			continue;
		}
		bool tracked = false;
		for (const auto& old : g_mapMarkerHandles) {
			if (old == handle) {
				tracked = true;
				break;
			}
		}
		if (!tracked) {
			player->currentMapMarkers.push_back(handle);
		}
		next.push_back(handle);
		g_peerMarkerRefs[peer.peer] = handle;
	}

	for (const auto& old : g_mapMarkerHandles) {
		bool keep = false;
		for (const auto& n : next) {
			if (old == n) {
				keep = true;
				break;
			}
		}
		if (!keep) {
			RemoveMapMarkerHandle(player, old);
		}
	}
	g_mapMarkerHandles = std::move(next);
	g_lastMapMarkers = g_mapMarkerHandles.size();

	if (!g_mapMarkerHandles.empty()) {
		if (auto* ui = RE::UI::GetSingleton()) {
			const auto menu = ui->GetMenu<RE::PipboyMenu>();
			if (menu) {
				menu->RefreshMapMarkers(0);
			}
		}
	}
}

}  // namespace cmp_indicators
