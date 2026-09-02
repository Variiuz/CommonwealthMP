#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
	class TESForm;
}

void CMP_SendAppearance(bool force);
void CMP_SendInventory(bool force);
void CMP_ApplyGhostAppearance(RE::Actor* actor, std::uint32_t peerId);
void CMP_ApplyGhostInventory(RE::Actor* actor, std::uint32_t peerId);
bool CMP_PaintGhostFromLocal(RE::Actor* actor);
void CMP_EquipGhostFallbackWeapon(RE::Actor* actor);
void CMP_StripGhostWorn(RE::Actor* actor);
RE::TESForm* CMP_ResolveForm(std::uint32_t rawId, const char* plugin);
