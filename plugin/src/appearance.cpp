#include "pch.h"
#include "cmp.h"
#include "udp_win.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

namespace {

constexpr std::uint32_t kAppearMagic = 0x45505041;
constexpr std::uint16_t kAppearVersion = 1;
constexpr int kMaxHead = 16;
constexpr int kMaxMorph = 32;
constexpr int kMaxTint = 32;
constexpr int kMaxEquip = 16;

using Writer = cmp::BlobWriter;
using Reader = cmp::BlobReader;
constexpr std::size_t kPluginField = cmp::kPluginField;

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

struct WornItem {
	RE::TESForm* form{ nullptr };
	std::uint8_t slot{ 0xFF };
};

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

	auto parts = npc->GetHeadParts(false);
	const int nHead = static_cast<int>(std::min<std::size_t>(parts.size(), kMaxHead));
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

const RE::BGSEquipSlot* EquipSlotFor(RE::TESForm* form)
{
	if (auto* weap = form ? form->As<RE::TESObjectWEAP>() : nullptr) {
		return weap->weaponData.equipSlot;
	}
	return nullptr;
}

void EquipForm(RE::Actor* actor, RE::TESForm* form, std::uint8_t bipedSlot = 0xFF)
{
	(void)bipedSlot;
	auto* mgr = RE::ActorEquipManager::GetSingleton();
	if (!mgr || !actor || !form || cmp::forbidden_actor_base(form->GetFormID())) {
		return;
	}
	RE::BGSObjectInstance inst{ form, nullptr };
	mgr->EquipObject(actor, inst, 0, 1, EquipSlotFor(form), false, true, false, true, false);
}

void CMP_StripGhostWorn(RE::Actor* actor)
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

	std::uint32_t raceId = 0;
	char racePlug[kPluginField]{};
	if (!r.u32(raceId) || !r.plugin(racePlug)) {
		return;
	}
	if (auto* race = CMP_ResolveForm(raceId, racePlug)) {
		if (auto* tesRace = race->As<RE::TESRace>()) {
			dest->SetFormRace(tesRace);
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
	if (dest->headParts && dest->numHeadParts > 0 && !parts.empty()) {
		const int n = std::min(static_cast<int>(parts.size()), static_cast<int>(dest->numHeadParts));
		for (int i = 0; i < n; ++i) {
			dest->headParts[i] = parts[static_cast<std::size_t>(i)];
		}
		dest->numHeadParts = static_cast<std::int8_t>(n);
	}

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
		CMP_StripGhostWorn(actor);
	}
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
		}
	}

	if (actor) {
		actor->UpdateReference3D();
	}
}

void SendChunks(cmp::Msg type, const std::vector<std::uint8_t>& blob, std::uint32_t peerId, const char* host, std::uint16_t port)
{
	std::vector<std::vector<std::uint8_t>> packets;
	if (!cmp::split_blob_chunks(type, peerId, blob, packets)) {
		return;
	}
	for (const auto& pkt : packets) {
		cmp_udp_send(host, port, pkt.data(), static_cast<int>(pkt.size()));
	}
}

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
		cmp::copy_cstr(sheet.name, sizeof(sheet.name), display ? display : "fo4");
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

}  // namespace

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

void CMP_SendAppearance(bool force)
{
	auto& s = CMP_Session();
	if (!s.joined || !s.myPeerId) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell()) {
		return;
	}

	using clock = std::chrono::steady_clock;
	const double t = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	const auto key = EquipKey(player);
	if (!force && s.lastAppearanceSend > 0.0 && (t - s.lastAppearanceSend) < 2.0 && key == s.lastEquipKey) {
		return;
	}
	s.lastAppearanceSend = t;
	s.lastEquipKey = key;

	std::vector<std::uint8_t> blob;
	if (!ExtractBlob(blob)) {
		return;
	}
	SendChunks(cmp::Msg::AppearanceChunk, blob, s.myPeerId, s.settings.host.c_str(), s.settings.port);
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
		auto it = s.appearances.find(peerId);
		if (it == s.appearances.end()) {
			return;
		}
		blob = it->second;
	}
	if (blob.size() < 8) {
		return;
	}
	Reader r{ std::span<const std::uint8_t>(blob.data(), blob.size()) };
	std::uint32_t magic = 0;
	std::uint16_t ver = 0;
	if (!r.u32(magic) || magic != kAppearMagic || !r.u16(ver)) {
		return;
	}
	auto* npc = actor->GetNPC();
	if (!npc) {
		return;
	}
	ApplyToNpc(npc, r, actor);
	REX::INFO("Applied appearance peer={} bytes={} onto {:08X}", peerId, blob.size(), npc->GetFormID());
}

void CMP_SendInventory(bool force)
{
	auto& s = CMP_Session();
	if (!s.joined || !s.myPeerId) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell()) {
		return;
	}

	using clock = std::chrono::steady_clock;
	const double t = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	const auto key = InvKey(player);
	if (!force && s.lastInventorySend > 0.0 && (t - s.lastInventorySend) < 3.0 && key == s.lastInvKey) {
		return;
	}
	s.lastInventorySend = t;
	s.lastInvKey = key;

	std::vector<std::uint8_t> blob;
	if (!ExtractInventory(blob)) {
		return;
	}
	SendChunks(cmp::Msg::InventoryChunk, blob, s.myPeerId, s.settings.host.c_str(), s.settings.port);
}

void CMP_ApplyGhostInventory(RE::Actor* actor, std::uint32_t peerId)
{
	if (!actor) {
		return;
	}
	std::vector<std::uint8_t> blob;
	{
		auto& s = CMP_Session();
		std::lock_guard lock(s.mutex);
		auto it = s.inventories.find(peerId);
		if (it == s.inventories.end()) {
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
			s.ghostNames[peerId] = sheet.name;
		}
		CMP_SetGhostLabel(actor, sheet.name);
	}

	int equipped = 0;
	int missed = 0;
	CMP_StripGhostWorn(actor);
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
	}
	REX::INFO("Applied inventory peer={} bytes={} name={} worn={} misses={}",
		peerId, blob.size(), sheet.name, equipped, missed);
}
