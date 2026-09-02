#include "pch.h"
#include "appearance.h"
#include "appearance/internal.h"
#include "ghost.h"
#include "session.h"

#include <chrono>
#include <vector>

namespace {

void PaintDummyGhostsFromLocal()
{
	std::vector<RE::ObjectRefHandle> handles;
	{
		auto& s = CMP_Session();
		std::lock_guard lock(s.mutex);
		for (const auto& [peer, handle] : s.ghosts.byPeer) {
			if (!cmp::is_fake_peer(peer)) {
				continue;
			}
			handles.push_back(handle);
		}
	}
	for (const auto& handle : handles) {
		const auto ptr = handle.get();
		auto* actor = ptr ? ptr->As<RE::Actor>() : nullptr;
		if (actor && actor->Get3D()) {
			CMP_PaintGhostFromLocal(actor);
		}
	}
}

}  // namespace

void CMP_EquipGhostFallbackWeapon(RE::Actor* actor)
{
	if (!actor) {
		return;
	}
	auto* form = RE::TESForm::GetFormByID(0x00004822);
	if (!form) {
		form = RE::TESForm::GetFormByID(0x000913CA);
	}
	if (!form) {
		return;
	}
	cmp_appearance::EquipForm(actor, form);
	if (actor->Get3D()) {
		actor->UpdateReference3D();
	}
	REX::INFO("Equipped fallback weapon {:08X} on ghost {:08X}", form->GetFormID(), actor->GetFormID());
}

bool CMP_PaintGhostFromLocal(RE::Actor* actor)
{
	if (!actor) {
		return false;
	}
	auto* npc = actor->GetNPC();
	if (!CMP_IsPaintableGhostBase(npc)) {
		return false;
	}
	std::vector<std::uint8_t> blob;
	if (!cmp_appearance::ExtractBlob(blob)) {
		return false;
	}
	if (!cmp_appearance::ApplyAppearanceBlob(actor, blob)) {
		return false;
	}
	if (!cmp_appearance::GhostHasWeapon(actor)) {
		CMP_EquipGhostFallbackWeapon(actor);
	}
	REX::INFO("Painted ghost from local {:08X} bytes={}", actor->GetFormID(), blob.size());
	return true;
}

RE::TESForm* CMP_ResolveForm(std::uint32_t rawId, const char* plugin)
{
	if (!rawId) {
		return nullptr;
	}
	if (rawId < 0x01000000) {
		return RE::TESForm::GetFormByID(rawId);
	}
	auto* data = RE::TESDataHandler::GetSingleton();
	if (!data || !plugin || !plugin[0]) {
		return nullptr;
	}
	const auto* file = data->LookupModByName(plugin);
	if (!file) {
		REX::INFO("Appearance plugin missing {} form {:X}", plugin, rawId);
		return nullptr;
	}
	if (file->IsLight()) {
		return data->LookupForm(rawId, plugin);
	}
	return RE::TESForm::GetFormByID(cmp::full_form_id(rawId, file->compileIndex, false));
}

void CMP_StripGhostWorn(RE::Actor* actor)
{
	cmp_appearance::StripGhostWorn(actor);
}

void CMP_SendAppearance(bool force)
{
	auto& s = CMP_Session();
	if (!s.net.joined || !s.net.myPeerId) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell()) {
		return;
	}

	using clock = std::chrono::steady_clock;
	const double t = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	const auto key = cmp_appearance::EquipKey(player);
	const auto prevKey = s.blobs.lastEquipKey;
	const bool first = s.blobs.lastAppearanceSend <= 0.0;
	if (!force && !first && (t - s.blobs.lastAppearanceSend) < 2.0 && key == s.blobs.lastEquipKey) {
		return;
	}
	s.blobs.lastAppearanceSend = t;
	s.blobs.lastEquipKey = key;

	std::vector<std::uint8_t> blob;
	if (!cmp_appearance::ExtractBlob(blob)) {
		return;
	}
	cmp_appearance::SendChunks(cmp::Msg::AppearanceChunk, blob, s.net.myPeerId, s.settings.host.c_str(), s.settings.port);
	if (force || first || key != prevKey) {
		PaintDummyGhostsFromLocal();
	}
}

void CMP_ApplyGhostAppearance(RE::Actor* actor, std::uint32_t peerId)
{
	if (!actor) {
		return;
	}
	std::vector<std::uint8_t> blob;
	{
		auto& s = CMP_Session();
		std::lock_guard lock(s.mutex);
		auto it = s.blobs.appearances.find(peerId);
		if (it == s.blobs.appearances.end()) {
			return;
		}
		blob = it->second;
	}
	if (!cmp_appearance::ApplyAppearanceBlob(actor, blob)) {
		return;
	}
	REX::INFO("Applied appearance peer={} bytes={} onto {:08X}", peerId, blob.size(), actor->GetFormID());
}

void CMP_SendInventory(bool force)
{
	auto& s = CMP_Session();
	if (!s.net.joined || !s.net.myPeerId) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell()) {
		return;
	}

	using clock = std::chrono::steady_clock;
	const double t = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	const auto key = cmp_appearance::InvKey(player);
	if (!force && s.blobs.lastInventorySend > 0.0 && (t - s.blobs.lastInventorySend) < 3.0 && key == s.blobs.lastInvKey) {
		return;
	}
	s.blobs.lastInventorySend = t;
	s.blobs.lastInvKey = key;

	std::vector<std::uint8_t> blob;
	if (!cmp_appearance::ExtractInventory(blob)) {
		return;
	}
	cmp_appearance::SendChunks(cmp::Msg::InventoryChunk, blob, s.net.myPeerId, s.settings.host.c_str(), s.settings.port);
}

void CMP_ApplyGhostInventory(RE::Actor* actor, std::uint32_t peerId)
{
	cmp_appearance::ApplyGhostInventory(actor, peerId);
}
