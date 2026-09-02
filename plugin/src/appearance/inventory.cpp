#include "pch.h"
#include "appearance.h"
#include "appearance/internal.h"
#include "ghost.h"
#include "session.h"

#include <algorithm>
#include <vector>

namespace cmp_appearance {

std::uint64_t InvKey(RE::Actor* actor)
{
	std::uint64_t h = 14695981039346656037ull;
	if (!actor || !actor->inventoryList) {
		return h;
	}
	for (auto& item : actor->inventoryList->data) {
		if (!item.object) {
			continue;
		}
		std::uint32_t count = 0;
		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
			count += stack->GetCount();
		}
		h ^= item.object->GetFormID();
		h *= 1099511628211ull;
		h ^= count;
		h *= 1099511628211ull;
	}
	return h;
}

bool ExtractInventory(std::vector<std::uint8_t>& out)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}
	auto* npc = player->GetNPC();

	cmp::InventorySheet sheet;
	const auto& sessionName = CMP_Session().settings.playerName;
	if (!sessionName.empty()) {
		cmp::copy_cstr(sheet.name, sizeof(sheet.name), sessionName);
	} else {
		const char* display = player->GetDisplayFullName();
		cmp::copy_cstr(sheet.name, sizeof(sheet.name), display && display[0] ? display : "Player");
	}
	sheet.sex = npc && npc->GetSex() == RE::SEX::kFemale ? 1 : 0;
	sheet.race = PackForm(npc ? npc->GetFormRace() : nullptr);

	std::vector<RE::TESForm*> worn;
	CollectWorn(player, worn);
	Uniq(worn);
	const int nWorn = static_cast<int>(std::min<std::size_t>(worn.size(), static_cast<std::size_t>(cmp::kMaxWorn)));
	for (int i = 0; i < nWorn; ++i) {
		sheet.worn.push_back(PackForm(worn[static_cast<std::size_t>(i)]));
	}

	if (player->inventoryList) {
		for (auto& item : player->inventoryList->data) {
			if (!item.object || sheet.stacks.size() >= static_cast<std::size_t>(cmp::kMaxStacks)) {
				continue;
			}
			if (cmp::forbidden_actor_base(item.object->GetFormID())) {
				continue;
			}
			std::int32_t count = 0;
			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
				count += static_cast<std::int32_t>(stack->GetCount());
			}
			if (count <= 0) {
				continue;
			}
			cmp::InvStack row;
			row.form = PackForm(item.object);
			row.count = static_cast<std::uint32_t>(count);
			sheet.stacks.push_back(row);
		}
	}
	return cmp::encode_inventory_sheet(sheet, out);
}

void ApplyGhostInventory(RE::Actor* actor, std::uint32_t peerId)
{
	if (!actor) {
		return;
	}
	std::vector<std::uint8_t> blob;
	{
		auto& s = CMP_Session();
		std::lock_guard lock(s.mutex);
		auto it = s.blobs.inventories.find(peerId);
		if (it == s.blobs.inventories.end()) {
			return;
		}
		blob = it->second;
	}
	cmp::InventorySheet sheet;
	if (!cmp::decode_inventory_sheet(blob, sheet)) {
		REX::INFO("Inventory blob rejected peer={} bytes={}", peerId, blob.size());
		return;
	}
	if (sheet.name[0]) {
		{
			auto& s = CMP_Session();
			std::lock_guard lock(s.mutex);
			s.ghosts.names[peerId] = sheet.name;
		}
		CMP_SetGhostLabel(actor, sheet.name);
	}

	int equipped = 0;
	int missed = 0;
	StripGhostWorn(actor);
	for (const auto& worn : sheet.worn) {
		auto* form = CMP_ResolveForm(worn.raw, worn.plugin);
		if (!form) {
			++missed;
			REX::INFO("Inventory miss worn {:X} {}", worn.raw, worn.plugin);
			continue;
		}
		if (cmp::forbidden_actor_base(form->GetFormID()) || form->As<RE::TESNPC>()) {
			REX::INFO("Inventory skip forbidden worn {:X}", form->GetFormID());
			continue;
		}
		EquipForm(actor, form);
		++equipped;
	}
	for (const auto& stack : sheet.stacks) {
		auto* form = CMP_ResolveForm(stack.form.raw, stack.form.plugin);
		if (!form) {
			++missed;
			REX::INFO("Inventory miss stack {:X} {} count={}", stack.form.raw, stack.form.plugin, stack.count);
			continue;
		}
		if (cmp::forbidden_actor_base(form->GetFormID())) {
			continue;
		}
	}

	if (actor->Get3D()) {
		actor->UpdateReference3D();
		if (auto* q = RE::TaskQueueInterface::GetSingleton()) {
			q->QueueUpdate3D(actor, 0);
		}
	}
	REX::INFO("Applied inventory peer={} bytes={} name={} worn={} misses={}",
		peerId, blob.size(), sheet.name, equipped, missed);
	CMP_ReapplyGhostPuppet(peerId);
}

}  // namespace cmp_appearance
