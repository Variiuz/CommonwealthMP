#include "pch.h"
#include "appearance.h"
#include "appearance/internal.h"
#include "ghost.h"

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

namespace cmp_appearance {

const RE::BGSEquipSlot* EquipSlotFor(RE::TESForm* form)
{
	if (auto* weap = form ? form->As<RE::TESObjectWEAP>() : nullptr) {
		return weap->weaponData.equipSlot;
	}
	return nullptr;
}

bool GhostHasForm(RE::Actor* actor, RE::TESForm* form)
{
	if (!actor || !form || !actor->inventoryList) {
		return false;
	}
	for (auto& item : actor->inventoryList->data) {
		if (item.object != form) {
			continue;
		}
		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
			if (stack->GetCount() > 0) {
				return true;
			}
		}
	}
	return false;
}

bool GhostHasWeapon(RE::Actor* actor)
{
	if (!actor) {
		return false;
	}
	std::vector<WornItem> worn;
	CollectWornItems(actor, worn);
	for (const auto& w : worn) {
		if (w.form && w.form->As<RE::TESObjectWEAP>()) {
			return true;
		}
	}
	return false;
}

bool IsSkippedGear(RE::TESForm* form)
{
	if (!form) {
		return true;
	}
	if (form->As<RE::TESFurniture>()) {
		return true;
	}
	if (auto* armo = form->As<RE::TESObjectARMO>()) {
		if (armo->HasKeywordString("ArmorTypePower")) {
			return true;
		}
	}
	return false;
}

void EquipForm(RE::Actor* actor, RE::TESForm* form, std::uint8_t bipedSlot)
{
	(void)bipedSlot;
	auto* mgr = RE::ActorEquipManager::GetSingleton();
	if (!mgr || !actor || !form || cmp::forbidden_actor_base(form->GetFormID()) || IsSkippedGear(form)) {
		return;
	}
	auto* bound = form->As<RE::TESBoundObject>();
	if (!bound) {
		return;
	}
	if (!GhostHasForm(actor, form)) {
		RE::BSTSmartPointer<RE::ExtraDataList> extra{ new RE::ExtraDataList() };
		actor->AddObjectToContainer(bound, extra, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
	}
	RE::BGSObjectInstance inst{ form, nullptr };
	mgr->EquipObject(actor, inst, 0, 1, EquipSlotFor(form), false, true, false, true, false);
}

void StripGhostWorn(RE::Actor* actor)
{
	auto* mgr = RE::ActorEquipManager::GetSingleton();
	if (!mgr || !actor) {
		return;
	}
	std::vector<RE::TESForm*> worn;
	CollectWorn(actor, worn);
	for (auto* form : worn) {
		if (!form || IsPipboyForm(actor, form)) {
			continue;
		}
		RE::BGSObjectInstance inst{ form, nullptr };
		mgr->UnequipObject(actor, &inst, 1, EquipSlotFor(form), 0, false, true, false, true, nullptr);
	}
}

void ApplyToNpc(RE::TESNPC* dest, Reader& r, RE::Actor* actor)
{
	if (!dest || cmp::forbidden_actor_base(dest->GetFormID())) {
		REX::WARN("Appearance refused on forbidden ActorBase");
		return;
	}
	if (!CMP_IsPaintableGhostBase(dest)) {
		const auto* file = dest->GetFile();
		const auto name = file ? file->GetFilename() : std::string_view{};
		REX::WARN("Appearance refused on non-clone ActorBase {:08X} ({})",
			dest->GetFormID(),
			name.empty() ? "no-file" : name);
		return;
	}

	if (actor) {
		StripGhostWorn(actor);
	}

	std::uint8_t sex = 0;
	std::uint8_t pad = 0;
	if (!r.u8(sex) || !r.u8(pad)) {
		return;
	}
	if (sex) {
		dest->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kFemale);
	} else {
		dest->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kFemale);
	}
	dest->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kUnique);
	dest->defOutfit = nullptr;
	dest->sleepOutfit = nullptr;

	std::uint32_t raceId = 0;
	char racePlug[kPluginField]{};
	if (!r.u32(raceId) || !r.plugin(racePlug)) {
		return;
	}
	RE::TESRace* appliedRace = nullptr;
	if (auto* race = CMP_ResolveForm(raceId, racePlug)) {
		if (auto* tesRace = race->As<RE::TESRace>()) {
			dest->SetFormRace(tesRace);
			appliedRace = tesRace;
			dest->originalRace = tesRace;
		}
	}

	if (!r.f32(dest->morphWeight.x) || !r.f32(dest->morphWeight.y) || !r.f32(dest->morphWeight.z) || !r.f32(dest->height)) {
		return;
	}

	std::uint32_t hairId = 0;
	char hairPlug[kPluginField]{};
	std::uint32_t faceId = 0;
	char facePlug[kPluginField]{};
	if (!r.u32(hairId) || !r.plugin(hairPlug) || !r.u32(faceId) || !r.plugin(facePlug)) {
		return;
	}
	if (auto* hair = CMP_ResolveForm(hairId, hairPlug)) {
		if (auto* col = hair->As<RE::BGSColorForm>()) {
			dest->SetHairColor(col);
		}
	}
	if (auto* face = CMP_ResolveForm(faceId, facePlug)) {
		if (auto* col = face->As<RE::BGSColorForm>()) {
			if (dest->headRelatedData) {
				dest->headRelatedData->facialHairColor = col;
			}
		}
	}

	std::uint8_t br = 0, bg = 0, bb = 0, ba = 0;
	if (!r.u8(br) || !r.u8(bg) || !r.u8(bb) || !r.u8(ba)) {
		return;
	}
	dest->bodyTintColorR = static_cast<std::int8_t>(br);
	dest->bodyTintColorG = static_cast<std::int8_t>(bg);
	dest->bodyTintColorB = static_cast<std::int8_t>(bb);
	dest->bodyTintColorA = static_cast<std::int8_t>(ba);

	std::uint8_t nHead = 0;
	if (!r.u8(nHead)) {
		return;
	}
	std::vector<RE::BGSHeadPart*> parts;
	for (std::uint8_t i = 0; i < nHead; ++i) {
		std::uint32_t id = 0;
		char plug[kPluginField]{};
		if (!r.u32(id) || !r.plugin(plug)) {
			return;
		}
		if (auto* form = CMP_ResolveForm(id, plug)) {
			if (auto* hp = form->As<RE::BGSHeadPart>()) {
				parts.push_back(hp);
			} else {
				REX::INFO("Appearance skip head part {:X} {}", id, plug);
			}
		} else if (id) {
			REX::INFO("Appearance miss head part {:X} {}", id, plug);
		}
	}
	// Write onto the clone's own headParts only. Do not touch
	// TESNPC::GetAlternateHeadPartListMap(): that global's CommonLib
	// relocation has been crashing RtlFreeHeap on join/appearance apply
	// Clones keep originalRace == formRace so the
	// engine does not read the alternate map for them anyway.
	dest->faceNPC = nullptr;
	if (!parts.empty()) {
		const int want = std::min(static_cast<int>(parts.size()), 127);
		if (!dest->headParts || dest->numHeadParts < want) {
			auto& mem = RE::MemoryManager::GetSingleton();
			auto* neu = static_cast<RE::BGSHeadPart**>(
				mem.Allocate(sizeof(RE::BGSHeadPart*) * static_cast<std::size_t>(want), 0, false));
			if (neu) {
				for (int i = 0; i < want; ++i) {
					neu[i] = nullptr;
				}
				if (dest->headParts) {
					mem.Deallocate(dest->headParts, false);
				}
				dest->headParts = neu;
				dest->numHeadParts = static_cast<std::int8_t>(want);
				REX::INFO("Appearance headParts realloc {} on {:08X}", want, dest->GetFormID());
			}
		}
		if (dest->headParts && dest->numHeadParts > 0) {
			const int n = std::min(want, static_cast<int>(dest->numHeadParts));
			for (int i = 0; i < n; ++i) {
				dest->headParts[i] = parts[static_cast<std::size_t>(i)];
			}
			for (int i = n; i < dest->numHeadParts; ++i) {
				dest->headParts[i] = nullptr;
			}
			dest->numHeadParts = static_cast<std::int8_t>(n);
		} else {
			REX::WARN("Appearance headParts array missing on {:08X} (numHeadParts={})", dest->GetFormID(), dest->numHeadParts);
		}
	}
	(void)appliedRace;

	std::uint8_t nMorph = 0;
	if (!r.u8(nMorph)) {
		return;
	}
	for (std::uint8_t i = 0; i < nMorph; ++i) {
		std::uint32_t key = 0;
		float value = 0.f;
		if (!r.u32(key) || !r.f32(value)) {
			return;
		}
		if (dest->morphSliderValues) {
			auto it = dest->morphSliderValues->find(key);
			if (it != dest->morphSliderValues->end()) {
				it->second = value;
			} else {
				dest->morphSliderValues->emplace(key, value);
			}
		}
	}

	std::uint8_t nTint = 0;
	if (!r.u8(nTint)) {
		return;
	}
	for (std::uint8_t i = 0; i < nTint; ++i) {
		std::uint16_t id = 0;
		std::uint8_t value = 0;
		std::uint8_t type = 0;
		std::uint32_t color = 0;
		if (!r.u16(id) || !r.u8(value) || !r.u8(type) || !r.u32(color)) {
			return;
		}
		if (!dest->tintingData) {
			continue;
		}
		for (auto* e : dest->tintingData->entriesA) {
			if (!e || e->idLink != id) {
				continue;
			}
			e->tingingValue = value;
			if (static_cast<RE::BGSCharacterTint::EntryType>(type) == RE::BGSCharacterTint::EntryType::kPalette) {
				if (e->GetType() == RE::BGSCharacterTint::EntryType::kPalette) {
					static_cast<RE::BGSCharacterTint::PaletteEntry*>(e)->tintingColor = color;
				}
			}
			break;
		}
	}

	std::uint8_t nEq = 0;
	if (!r.u8(nEq)) {
		return;
	}
	if (actor) {
		StripGhostWorn(actor);
	}
	int appliedGear = 0;
	for (std::uint8_t i = 0; i < nEq; ++i) {
		std::uint32_t id = 0;
		char plug[kPluginField]{};
		std::uint8_t slot = 0;
		if (!r.u32(id) || !r.plugin(plug) || !r.u8(slot)) {
			return;
		}
		auto* form = CMP_ResolveForm(id, plug);
		if (!form) {
			if (id) {
				REX::INFO("Appearance miss armor {:X} {}", id, plug);
			}
			continue;
		}
		if (actor) {
			EquipForm(actor, form, slot);
			++appliedGear;
		}
	}

	if (actor) {
		actor->UpdateReference3D();
		if (auto* q = RE::TaskQueueInterface::GetSingleton()) {
			q->QueueUpdate3D(actor, 0);
		}
	}
	REX::INFO("Appearance applied actor={:08X} sex={} race={:08X} head={} morph={} tint={} gear={}",
		actor ? actor->GetFormID() : 0,
		static_cast<int>(sex),
		raceId,
		parts.size(),
		static_cast<int>(nMorph),
		static_cast<int>(nTint),
		appliedGear);
}

bool ApplyAppearanceBlob(RE::Actor* actor, const std::vector<std::uint8_t>& blob)
{
	if (!actor || !actor->IsActor() || blob.size() < 8) {
		return false;
	}
	Reader r{ std::span<const std::uint8_t>(blob.data(), blob.size()) };
	std::uint32_t magic = 0;
	std::uint16_t ver = 0;
	if (!r.u32(magic) || magic != kAppearMagic || !r.u16(ver)) {
		return false;
	}
	auto* npc = actor->GetNPC();
	if (!npc) {
		REX::WARN("Appearance refused: actor {:08X} has no ActorBase", actor->GetFormID());
		return false;
	}
	ApplyToNpc(npc, r, actor);
	return true;
}

}  // namespace cmp_appearance
