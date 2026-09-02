#include "pch.h"
#include "ghost.h"
#include "ghost/internal.h"
#include "session.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>

void CMP_SetGhostLabel(RE::Actor* actor, const char* name)
{
	if (!actor || !name || !name[0]) {
		return;
	}
	if (actor->extraList) {
		actor->extraList->SetOverrideName(name);
	}
	if (auto* npc = actor->GetNPC()) {
		RE::TESFullName::SetFullName(*npc, name);
	}
}

void CMP_DrawGhostNameplates(ImDrawList* drawList, float viewportW, float viewportH)
{
	if (!drawList || viewportW <= 0.0f || viewportH <= 0.0f) {
		return;
	}
	auto* camera = RE::Main::WorldRootCamera();
	if (!camera) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	auto& s = CMP_Session();
	const auto loc = cmp_ghost::PlayerLocationForm();
	const auto playerPos = player->GetPosition();

	std::unordered_map<std::uint32_t, cmp::PlayerPose> poses;
	std::unordered_map<std::uint32_t, RE::ObjectRefHandle> ghosts;
	{
		std::lock_guard lock(s.mutex);
		if (!s.net.joined) {
			return;
		}
		poses = s.net.latestPose;
		ghosts = s.ghosts.byPeer;
	}

	for (const auto& [peer, pose] : poses) {
		if (peer == s.net.myPeerId) {
			continue;
		}
		if (pose.locationFormId != 0 && loc != 0 && pose.locationFormId != loc) {
			continue;
		}

		auto it = ghosts.find(peer);
		if (it == ghosts.end()) {
			continue;
		}
		const auto ptr = it->second.get();
		if (!ptr) {
			continue;
		}
		auto* actor = ptr->As<RE::Actor>();
		if (!actor || !actor->Get3D()) {
			continue;
		}

		const auto pos = actor->GetPosition();
		const float dx = pos.x - playerPos.x;
		const float dy = pos.y - playerPos.y;
		const float dz = pos.z - playerPos.z;
		const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

		RE::NiPoint3 head = pos;
		float headOffset = 128.0f * actor->GetScale();
		if (auto* npc = actor->GetNPC(); npc && npc->height > 0.1f) {
			headOffset = npc->height * actor->GetScale();
		}
		head.z += headOffset;

		float sx = 0.0f;
		float sy = 0.0f;
		float sz = 0.0f;
		if (!camera->WorldPtToScreenPt3(head, sx, sy, sz, 1e-5f)) {
			continue;
		}
		if (sz <= 0.0f || sz >= 1.0f) {
			continue;
		}

		const float screenX = sx * viewportW;
		const float screenY = (1.0f - sy) * viewportH;
		if (screenX < -50.0f || screenX > viewportW + 50.0f || screenY < -50.0f || screenY > viewportH + 50.0f) {
			continue;
		}

		const std::string label = cmp_ghost::GhostLabel(pose);
		const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
		const float x = screenX - textSize.x * 0.5f;
		const float y = screenY - textSize.y - 4.0f;

		ImU32 alpha = 255;
		if (dist > 80.0f) {
			const float fade = std::clamp(1.0f - (dist - 80.0f) / 120.0f, 0.15f, 1.0f);
			alpha = static_cast<ImU32>(fade * 255.0f);
		}
		const ImU32 col = IM_COL32(255, 255, 255, alpha);
		const ImU32 shadow = IM_COL32(0, 0, 0, alpha);

		drawList->AddText(ImVec2(x + 1.0f, y + 1.0f), shadow, label.c_str());
		drawList->AddText(ImVec2(x, y), col, label.c_str());
	}
}
