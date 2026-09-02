#pragma once

#include <cstdint>
#include <string>

struct ImDrawList;

namespace RE
{
	class Actor;
	class TESNPC;
}

void CMP_ApplyGhosts();
void CMP_DespawnGhosts();
void CMP_FreezeRemoteActor(RE::Actor* actor, std::uint32_t id);
void CMP_UnfreezeRemoteActor(RE::Actor* actor, std::uint32_t id);
void CMP_ReapplyGhostPuppet(std::uint32_t peerId);
bool CMP_IsPaintableGhostBase(RE::TESNPC* npc);
void CMP_SetGhostLabel(RE::Actor* actor, const char* name);
void CMP_DrawGhostNameplates(ImDrawList* drawList, float viewportW, float viewportH);
std::uint32_t CMP_PeerForGhost(RE::Actor* actor);
int CMP_CountGhostsWith3D();
bool CMP_ForceAnim(int step, std::string& note);
