#include "pch.h"
#include "companions.h"
#include "papyrus_util.h"

namespace {

void DismissActorIfCommanded(RE::Actor* actor, RE::Actor* player, int& dismissed)
{
	if (!actor || actor == player) {
		return;
	}
	if (!actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kIsCommandedActor)) {
		return;
	}
	CMP_CallActorPapyrus(actor, "DisallowCompanion", true);
	++dismissed;
	REX::INFO("Dismissed companion {:08X}", actor->GetFormID());
}

void ScanActorHandles(const RE::BSTArray<RE::ActorHandle>& handles, RE::Actor* player, int& dismissed)
{
	for (const auto& handle : handles) {
		const auto ptr = handle.get();
		if (!ptr) {
			continue;
		}
		DismissActorIfCommanded(ptr.get(), player, dismissed);
	}
}

}  // namespace

void CMP_DismissCompanionsOnJoin()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || player->teammateCount == 0) {
		return;
	}
	auto* lists = RE::ProcessLists::GetSingleton();
	if (!lists) {
		return;
	}

	int dismissed = 0;
	ScanActorHandles(lists->highActorHandles, player, dismissed);
	ScanActorHandles(lists->middleHighActorHandles, player, dismissed);
	ScanActorHandles(lists->middleLowActorHandles, player, dismissed);

	if (dismissed > 0) {
		REX::INFO("CMP_DismissCompanionsOnJoin dismissed {} companion(s)", dismissed);
	} else if (player->teammateCount > 0) {
		REX::WARN("CMP_DismissCompanionsOnJoin teammateCount={} but no commanded actor found", player->teammateCount);
	}
}
