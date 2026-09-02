#pragma once

#include "session.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace RE {
class Actor;
class TESNPC;
class TESForm;
}

namespace cmp_ghost {

extern std::mutex g_cloneMutex;
extern std::unordered_map<std::uint32_t, RE::TESNPC*> g_peerBases;
extern std::unordered_set<std::uint32_t> g_cloneFormIds;
extern std::unordered_set<std::uint32_t> g_ghostReady;
extern std::unordered_set<std::uint32_t> g_dummyArmed;
extern std::unordered_map<std::uint32_t, int> g_freezeTicks;
extern std::unordered_map<std::uint32_t, double> g_lastMoveSec;
extern bool g_cloneSourceFailed;

std::uint32_t PlayerLocationForm();
void SetGhostNote(std::string note);
std::string GhostLabel(const cmp::PlayerPose& pose);
void FreezeGhost(RE::Actor* actor, std::uint32_t peerId);

RE::TESNPC* NpcFromForm(RE::TESForm* form);
void SanitizeCloneFlags(RE::TESNPC* npc);
RE::TESNPC* FindCloneSource();
RE::TESNPC* FinalizeClone(RE::TESNPC* npc, RE::TESNPC* source, std::uint32_t peerId, const char* path);
RE::TESNPC* CloneGhostBase(std::uint32_t peerId);
void DropPeerClone(std::uint32_t peerId);
void ClearAllClones();

void EnsureGhost3D(RE::Actor* actor);
void MaybeEquipDummy(RE::Actor* actor, const cmp::PlayerPose& pose);
void FinishGhostSetup(RE::Actor* actor, const cmp::PlayerPose& pose, const char* path);
RE::Actor* SpawnGhostNative(const cmp::PlayerPose& pose);
RE::Actor* SpawnGhost(const cmp::PlayerPose& pose);
void MoveGhost(RE::Actor* actor, const cmp::PlayerPose& pose);

}  // namespace cmp_ghost
