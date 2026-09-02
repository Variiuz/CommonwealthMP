#pragma once

#include "cmp_blobs.hpp"
#include "cmp_protocol.hpp"
#include "session.h"

#include <cstdint>
#include <vector>

namespace RE {
class Actor;
class BGSEquipSlot;
class TESForm;
class TESNPC;
}

namespace cmp_appearance {

constexpr std::uint32_t kAppearMagic = 0x45505041;
constexpr std::uint16_t kAppearVersion = 1;
constexpr int kMaxHead = 16;
constexpr int kMaxMorph = 32;
constexpr int kMaxTint = 32;
constexpr int kMaxEquip = 16;

using Writer = cmp::BlobWriter;
using Reader = cmp::BlobReader;
constexpr std::size_t kPluginField = cmp::kPluginField;

struct WornItem {
	RE::TESForm* form{ nullptr };
	std::uint8_t slot{ 0xFF };
};

cmp::PackedForm PackForm(RE::TESForm* form);
void WriteForm(Writer& w, RE::TESForm* form);
std::uint64_t EquipKey(RE::Actor* actor);

bool IsPipboyForm(RE::Actor* actor, RE::TESForm* form);
void CollectWornItems(RE::Actor* actor, std::vector<WornItem>& out);
void CollectWorn(RE::Actor* actor, std::vector<RE::TESForm*>& out);
void Uniq(std::vector<RE::TESForm*>& forms);

bool ExtractBlob(std::vector<std::uint8_t>& out);
void SendChunks(cmp::Msg type, const std::vector<std::uint8_t>& blob, std::uint32_t peerId, const char* host, std::uint16_t port);

const RE::BGSEquipSlot* EquipSlotFor(RE::TESForm* form);
bool GhostHasForm(RE::Actor* actor, RE::TESForm* form);
bool GhostHasWeapon(RE::Actor* actor);
bool IsSkippedGear(RE::TESForm* form);
void EquipForm(RE::Actor* actor, RE::TESForm* form, std::uint8_t bipedSlot = 0xFF);
void StripGhostWorn(RE::Actor* actor);

void ApplyToNpc(RE::TESNPC* dest, Reader& r, RE::Actor* actor);
bool ApplyAppearanceBlob(RE::Actor* actor, const std::vector<std::uint8_t>& blob);

std::uint64_t InvKey(RE::Actor* actor);
bool ExtractInventory(std::vector<std::uint8_t>& out);
void ApplyGhostInventory(RE::Actor* actor, std::uint32_t peerId);

}  // namespace cmp_appearance
