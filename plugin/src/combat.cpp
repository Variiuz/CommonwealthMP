#include "pch.h"
#include "session.h"
#include "combat.h"
#include "ghost.h"
#include "net.h"
#include "udp_win.h"

#include <chrono>
#include <unordered_map>

namespace {

void OnHitEvent(const RE::TESHitEvent& ev);

class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent& a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
	{
		OnHitEvent(a_event);
		return RE::BSEventNotifyControl::kContinue;
	}
};

HitSink g_hitSink;
bool g_hitReady{ false };
std::unordered_map<std::uint32_t, double> g_lastHitSend;
double g_lastHitDropLog{ 0.0 };

double NowSec()
{
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

void RestoreGhostHealth(RE::Actor* actor)
{
	if (!actor) {
		return;
	}
	auto* av = RE::ActorValue::GetSingleton();
	if (!av || !av->health) {
		return;
	}
	const float maxHp = actor->GetPermanentActorValue(*av->health);
	if (maxHp > 0.0f) {
		actor->RestoreActorValue(*av->health, maxHp);
	}
}

bool LocalPlayerFiring()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}
	const auto gun = player->gunState;
	return gun == RE::GUN_STATE::kFire || gun == RE::GUN_STATE::kFireSighted
		|| player->meleeAttackState != 0;
}

RE::Actor* ResolveAggressor(const RE::TESHitEvent& ev)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return nullptr;
	}
	if (const auto handle = ev.hitData.aggressor; handle) {
		const auto ptr = handle.get();
		if (ptr) {
			if (auto* actor = ptr->As<RE::Actor>()) {
				return actor;
			}
		}
	}
	if (const auto causeRef = ev.cause.get()) {
		if (auto* cause = causeRef->As<RE::Actor>()) {
			return cause;
		}
	}
	if (LocalPlayerFiring()) {
		return player;
	}
	return nullptr;
}

void LogHitDrop(const char* reason, RE::Actor* aggressor, RE::Actor* cause, float damage)
{
	const double now = NowSec();
	if (now - g_lastHitDropLog < 1.0) {
		return;
	}
	g_lastHitDropLog = now;
	REX::INFO("Hit dropped: {} dmg={:.1f} aggressor={:08X} cause={:08X}",
		reason,
		damage,
		aggressor ? aggressor->GetFormID() : 0u,
		cause ? cause->GetFormID() : 0u);
}

void OnHitEvent(const RE::TESHitEvent& ev)
{
	auto& s = CMP_Session();
	if (!s.net.joined) {
		return;
	}
	if ((s.menu.sessionFlags & cmp::kSessionPvpEnabled) == 0) {
		return;
	}
	auto* targetRef = ev.target.get();
	auto* target = targetRef ? targetRef->As<RE::Actor>() : nullptr;
	if (!target || target->IsPlayerRef()) {
		return;
	}

	const auto peer = CMP_PeerForGhost(target);
	if (!peer || cmp::is_fake_peer(peer)) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* causeRef = ev.cause.get();
	auto* cause = causeRef ? causeRef->As<RE::Actor>() : nullptr;
	auto* aggressor = ResolveAggressor(ev);
	if (!aggressor || aggressor != player) {
		RestoreGhostHealth(target);
		LogHitDrop("not local player", aggressor, cause, ev.hitData.healthDamage);
		return;
	}

	float damage = 0.0f;
	if (ev.usesHitData) {
		damage = ev.hitData.healthDamage;
		if (!(damage > 0.01f)) {
			damage = ev.hitData.totalDamage;
		}
		if (!(damage > 0.01f)) {
			damage = ev.hitData.physicalDamage;
		}
	}
	damage = cmp::clamp_hit_damage(damage);
	RestoreGhostHealth(target);
	if (damage <= 0.0f || s.net.myPeerId == 0 || peer == s.net.myPeerId) {
		if (damage <= 0.0f) {
			LogHitDrop("zero damage", aggressor, cause, damage);
		}
		return;
	}

	const double now = NowSec();
	if (auto it = g_lastHitSend.find(peer); it != g_lastHitSend.end() && now - it->second < 0.05) {
		return;
	}
	g_lastHitSend[peer] = now;

	const auto msg = cmp::make_hit(s.net.myPeerId, peer, damage);
	CMP_Reliable_Send(&msg, static_cast<int>(sizeof(msg)));
}

}  // namespace

void CMP_OnHit(const cmp::Hit& hit)
{
	auto& s = CMP_Session();
	if (!s.net.joined || hit.targetPeerId != s.net.myPeerId) {
		return;
	}
	if ((s.menu.sessionFlags & cmp::kSessionPvpEnabled) == 0) {
		return;
	}
	const float damage = cmp::clamp_hit_damage(hit.damage);
	if (damage <= 0.0f) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || player->IsDead(true)) {
		return;
	}
	player->HandleHealthDamage(nullptr, damage);
	REX::INFO("Hit from peer {} dmg={:.1f}", hit.attackerPeerId, damage);
}

void CMP_InstallCombat()
{
	if (g_hitReady) {
		return;
	}
	auto* src = RE::TESHitEvent::GetEventSource();
	if (!src) {
		return;
	}
	src->RegisterSink(&g_hitSink);
	g_hitReady = true;
	REX::INFO("TESHitEvent sink ready");
}
