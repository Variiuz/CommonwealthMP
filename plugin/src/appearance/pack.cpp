#include "pch.h"
#include "appearance/internal.h"
#include "net.h"

#include <algorithm>
#include <vector>

namespace cmp_appearance {

cmp::PackedForm PackForm(RE::TESForm* form)
{
	if (!form) {
		return {};
	}
	auto* file = form->GetFile();
	const char* plugin = "Fallout4.esm";
	bool light = false;
	std::uint32_t id = form->GetFormID();
	if (file && file->filename[0]) {
		plugin = file->filename;
		light = file->IsLight();
		if (light) {
			id = form->GetLocalFormID();
		}
	}
	return cmp::pack_form_id(id, plugin, light);
}

void WriteForm(Writer& w, RE::TESForm* form)
{
	w.write_form(PackForm(form));
}

std::uint64_t EquipKey(RE::Actor* actor)
{
	std::uint64_t h = 14695981039346656037ull;
	if (!actor || !actor->inventoryList) {
		return h;
	}
	for (auto& item : actor->inventoryList->data) {
		if (!item.object) {
			continue;
		}
		for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
			if (!stack->IsEquipped()) {
				continue;
			}
			const auto id = item.object->GetFormID();
			h ^= id;
			h *= 1099511628211ull;
		}
	}
	return h;
}

bool IsPipboyForm(RE::Actor* actor, RE::TESForm* form)
{
	if (!actor || !form || !actor->biped) {
		return false;
	}
	return actor->biped->object[static_cast<int>(RE::BIPED_OBJECT::kPipboy)].parent.object == form;
}

void CollectWornItems(RE::Actor* actor, std::vector<WornItem>& out)
{
	out.clear();
	if (!actor) {
		return;
	}
	if (actor->inventoryList) {
		for (auto& item : actor->inventoryList->data) {
			if (!item.object || IsPipboyForm(actor, item.object)) {
				continue;
			}
			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
				if (stack->IsEquipped()) {
					out.push_back({ item.object, 0xFF });
					break;
				}
			}
		}
	}
	if (actor->biped) {
		for (int i = 0; i < static_cast<int>(RE::BIPED_OBJECT::kTotal); ++i) {
			if (i == static_cast<int>(RE::BIPED_OBJECT::kPipboy)) {
				continue;
			}
			auto* form = actor->biped->object[i].parent.object;
			if (!form) {
				continue;
			}
			bool found = false;
			for (auto& w : out) {
				if (w.form == form) {
					if (w.slot == 0xFF) {
						w.slot = static_cast<std::uint8_t>(i);
					}
					found = true;
					break;
				}
			}
			if (!found) {
				out.push_back({ form, static_cast<std::uint8_t>(i) });
			}
		}
	}
}

void CollectWorn(RE::Actor* actor, std::vector<RE::TESForm*>& out)
{
	std::vector<WornItem> items;
	CollectWornItems(actor, items);
	out.clear();
	for (const auto& w : items) {
		out.push_back(w.form);
	}
}

void Uniq(std::vector<RE::TESForm*>& forms)
{
	std::vector<RE::TESForm*> out;
	for (auto* f : forms) {
		if (!f) {
			continue;
		}
		if (std::find(out.begin(), out.end(), f) == out.end()) {
			out.push_back(f);
		}
	}
	forms.swap(out);
}

bool ExtractBlob(std::vector<std::uint8_t>& out)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}
	auto* npc = player->GetNPC();
	if (!npc) {
		return false;
	}

	Writer w;
	w.u32(kAppearMagic);
	w.u16(kAppearVersion);
	const auto sex = npc->GetSex();
	w.u8(sex == RE::SEX::kFemale ? 1 : 0);
	w.u8(0);
	WriteForm(w, npc->GetFormRace());
	w.f32(npc->morphWeight.x);
	w.f32(npc->morphWeight.y);
	w.f32(npc->morphWeight.z);
	w.f32(npc->height);
	RE::TESForm* hair = nullptr;
	RE::TESForm* facial = nullptr;
	if (npc->headRelatedData) {
		hair = npc->headRelatedData->hairColor;
		facial = npc->headRelatedData->facialHairColor;
	}
	WriteForm(w, hair);
	WriteForm(w, facial);
	w.u8(static_cast<std::uint8_t>(npc->bodyTintColorR));
	w.u8(static_cast<std::uint8_t>(npc->bodyTintColorG));
	w.u8(static_cast<std::uint8_t>(npc->bodyTintColorB));
	w.u8(static_cast<std::uint8_t>(npc->bodyTintColorA));

	auto parts = npc->GetHeadParts(true);
	if (parts.empty()) {
		parts = npc->GetHeadParts(false);
	}
	const int nHead = static_cast<int>(std::min<std::size_t>(parts.size(), kMaxHead));
	REX::INFO("Appearance extract headParts={}", nHead);
	w.u8(static_cast<std::uint8_t>(nHead));
	for (int i = 0; i < nHead; ++i) {
		WriteForm(w, parts[static_cast<std::size_t>(i)]);
	}

	std::uint8_t nMorph = 0;
	const auto morphAt = w.bytes.size();
	w.u8(0);
	if (npc->morphSliderValues) {
		for (const auto& kv : *npc->morphSliderValues) {
			if (nMorph >= kMaxMorph) {
				break;
			}
			w.u32(kv.first);
			w.f32(kv.second);
			++nMorph;
		}
	}
	w.bytes[morphAt] = nMorph;

	auto* tints = player->tintingData ? player->tintingData : npc->tintingData;
	std::uint8_t nTint = 0;
	const auto tintAt = w.bytes.size();
	w.u8(0);
	if (tints) {
		for (auto* e : tints->entriesA) {
			if (!e || nTint >= kMaxTint) {
				continue;
			}
			w.u16(e->idLink);
			w.u8(e->tingingValue);
			w.u8(static_cast<std::uint8_t>(e->GetType()));
			std::uint32_t color = 0;
			if (e->GetType() == RE::BGSCharacterTint::EntryType::kPalette) {
				color = static_cast<RE::BGSCharacterTint::PaletteEntry*>(e)->tintingColor;
			}
			w.u32(color);
			++nTint;
		}
	}
	w.bytes[tintAt] = nTint;

	std::vector<WornItem> worn;
	CollectWornItems(player, worn);
	const int nEq = static_cast<int>(std::min<std::size_t>(worn.size(), kMaxEquip));
	w.u8(static_cast<std::uint8_t>(nEq));
	for (int i = 0; i < nEq; ++i) {
		WriteForm(w, worn[static_cast<std::size_t>(i)].form);
		w.u8(worn[static_cast<std::size_t>(i)].slot);
	}

	out = std::move(w.bytes);
	return !out.empty();
}

void SendChunks(cmp::Msg type, const std::vector<std::uint8_t>& blob, std::uint32_t peerId, const char* host, std::uint16_t port)
{
	std::vector<std::vector<std::uint8_t>> packets;
	if (!cmp::split_blob_chunks(type, peerId, blob, packets)) {
		return;
	}
	(void)host;
	(void)port;
	for (const auto& pkt : packets) {
		CMP_Net_Send(pkt.data(), static_cast<int>(pkt.size()));
	}
}

}  // namespace cmp_appearance
