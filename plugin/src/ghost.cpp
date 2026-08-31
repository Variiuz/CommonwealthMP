#include "pch.h"
#include "cmp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace RE::BSScript
{
	IStackCallbackFunctor::~IStackCallbackFunctor() = default;
}

namespace {

std::uint32_t PlayerLocationForm()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return 0;
	}
	auto* cell = player->GetParentCell();
	if (!cell) {
		return 0;
	}
	if (cell->IsInterior()) {
		return cell->GetFormID();
	}
	if (cell->worldSpace) {
		return cell->worldSpace->GetFormID();
	}
	return cell->GetFormID();
}

std::mutex g_cloneMutex;
std::unordered_map<std::uint32_t, RE::TESNPC*> g_peerBases;
std::unordered_set<std::uint32_t> g_cloneFormIds;
std::unordered_set<std::uint32_t> g_ghostReady;
std::unordered_map<std::uint32_t, int> g_freezeTicks;
std::unordered_map<std::uint32_t, double> g_lastMoveSec;
bool g_cloneSourceFailed = false;

void SetGhostNote(std::string note)
{
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	s.lastGhostNote = std::move(note);
}

RE::TESNPC* NpcFromForm(RE::TESForm* form)
{
	auto* npc = form ? form->As<RE::TESNPC>() : nullptr;
	if (!npc || cmp::forbidden_actor_base(npc->GetFormID())) {
		return nullptr;
	}
	return npc;
}

void SanitizeCloneFlags(RE::TESNPC* npc)
{
	if (!npc) {
		return;
	}
	npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kUnique);
	npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kIsChargenFacePreset);
	npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kUsesTemplate);
}

RE::TESNPC* FindCloneSource()
{
	if (g_cloneSourceFailed) {
		return nullptr;
	}
	const auto formId = CMP_Session().settings.ghostSourceForm;
	auto* npc = NpcFromForm(RE::TESForm::GetFormByID(formId));
	if (!npc) {
		g_cloneSourceFailed = true;
		REX::ERROR("Ghost clone source {:08X} missing or forbidden", formId);
		SetGhostNote("ghost SourceForm missing");
		return nullptr;
	}
	return npc;
}

RE::TESNPC* FinalizeClone(RE::TESNPC* npc, RE::TESNPC* source, std::uint32_t peerId, const char* path)
{
	if (!npc) {
		return nullptr;
	}
	npc->SetTemporary();
	SanitizeCloneFlags(npc);
	if (auto* data = RE::TESDataHandler::GetSingleton()) {
		if (!data->AddFormToDataHandler(npc)) {
			REX::WARN("AddFormToDataHandler failed for clone {:08X}", npc->GetFormID());
		}
	}
	npc->InitItemImpl();
	g_cloneFormIds.insert(npc->GetFormID());
	REX::INFO("CloneGhostBase peer={} form={:08X} via {} source={:08X} unique={} race={:08X}",
		peerId,
		npc->GetFormID(),
		path,
		source->GetFormID(),
		npc->IsUnique(),
		npc->GetFormRace() ? npc->GetFormRace()->GetFormID() : 0);
	return npc;
}

RE::TESNPC* CloneGhostBase(std::uint32_t peerId)
{
	std::lock_guard lock(g_cloneMutex);
	if (auto it = g_peerBases.find(peerId); it != g_peerBases.end() && it->second) {
		return it->second;
	}

	auto* source = FindCloneSource();
	if (!source) {
		return nullptr;
	}

	RE::TESNPC* npc = nullptr;
	if (auto* dup = source->CreateDuplicateForm(false, nullptr)) {
		npc = FinalizeClone(dup->As<RE::TESNPC>(), source, peerId, "CreateDuplicateForm");
		if (!npc) {
			REX::WARN("CreateDuplicateForm did not yield TESNPC for peer={}", peerId);
		}
	}

	if (!npc) {
		auto* factory = RE::ConcreteFormFactory<RE::TESNPC>::GetFormFactory();
		if (!factory) {
			REX::ERROR("ConcreteFormFactory<TESNPC> missing");
			SetGhostNote("TESNPC factory missing");
			return nullptr;
		}
		auto* created = factory->Create();
		if (!created) {
			REX::ERROR("TESNPC factory Create failed peer={}", peerId);
			SetGhostNote("TESNPC Create failed");
			return nullptr;
		}
		created->Copy(source);
		npc = FinalizeClone(created, source, peerId, "FactoryCreate+Copy");
	}

	if (!npc) {
		SetGhostNote("ghost clone failed");
		return nullptr;
	}
	g_peerBases[peerId] = npc;
	return npc;
}

void DropPeerClone(std::uint32_t peerId)
{
	std::lock_guard lock(g_cloneMutex);
	if (auto it = g_peerBases.find(peerId); it != g_peerBases.end()) {
		if (it->second) {
			g_cloneFormIds.erase(it->second->GetFormID());
		}
		g_peerBases.erase(it);
	}
	g_ghostReady.erase(peerId);
	g_freezeTicks.erase(peerId);
	g_lastMoveSec.erase(peerId);
	CMP_ResetGhostPuppet(peerId);
}

void ClearAllClones()
{
	std::lock_guard lock(g_cloneMutex);
	g_peerBases.clear();
	g_cloneFormIds.clear();
	g_ghostReady.clear();
	g_freezeTicks.clear();
	g_lastMoveSec.clear();
	g_cloneSourceFailed = false;
	CMP_ResetAllPuppets();
}

void EnsureGhost3D(RE::Actor* actor)
{
	if (!actor) {
		return;
	}
	if (auto* player = RE::PlayerCharacter::GetSingleton()) {
		if (auto* cell = player->GetParentCell()) {
			if (actor->GetParentCell() != cell) {
				actor->SetParentCell(cell);
			}
		}
	}
	if (actor->IsDisabled()) {
		actor->Enable(true);
	}
	actor->DoMoveToHigh();
	actor->Update3DPosition(true);
	actor->UpdateReference3D();
	if (auto* q = RE::TaskQueueInterface::GetSingleton()) {
		q->QueueUpdate3D(actor, 0);
	}
}

template <class... Args>
void CallActorPapyrus(RE::Actor* actor, const char* fn, Args&&... args)
{
	auto* game = RE::GameVM::GetSingleton();
	if (!game || !actor) {
		return;
	}
	auto vm = game->GetVM();
	if (!vm) {
		return;
	}
	auto& handles = vm->GetObjectHandlePolicy();
	const auto handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::Actor>(), actor);
	if (handle == handles.EmptyHandle()) {
		return;
	}
	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb{};
	vm->DispatchMethodCall(
		static_cast<std::uint64_t>(handle),
		RE::BSFixedString("Actor"),
		RE::BSFixedString(fn),
		cb,
		std::forward<Args>(args)...);
}

void FreezeGhost(RE::Actor* actor, std::uint32_t peerId)
{
	if (!actor) {
		return;
	}

	actor->StopCombat();
	// Do not set kMovementBlocked: it freezes locomotion while we SetPosition the root.
	actor->boolFlags.set(
		RE::Actor::BOOL_FLAGS::kAttackingDisabled,
		RE::Actor::BOOL_FLAGS::kCastingDisabled,
		RE::Actor::BOOL_FLAGS::kDoNotShowOnStealthMeter,
		RE::Actor::BOOL_FLAGS::kShouldAnimGraphUpdate);
	actor->boolFlags.reset(RE::Actor::BOOL_FLAGS::kAttackOnSight);
	actor->boolFlags.reset(RE::Actor::BOOL_FLAGS::kMovementBlocked);

	int& tick = g_freezeTicks[peerId];
	++tick;
	if (tick == 1 || (tick % 30) == 0) {
		actor->SetLifeState(RE::ACTOR_LIFE_STATE::kAlive);
		CallActorPapyrus(actor, "EnableAI", false);
	}
}

std::string GhostLabel(const cmp::PlayerPose& pose)
{
	std::string label = "Player";
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	if (pose.peerId == s.fakePeerId || pose.peerId == cmp::kFakePeerId) {
		label = "Dummy";
	} else if (auto it = s.ghostNames.find(pose.peerId); it != s.ghostNames.end() && !it->second.empty()) {
		label = it->second;
	}
	return label;
}

void FinishGhostSetup(RE::Actor* actor, const cmp::PlayerPose& pose, const char* path)
{
	if (!actor) {
		return;
	}
	REX::INFO("FinishGhostSetup begin {:08X} peer={} via {}", actor->GetFormID(), pose.peerId, path);
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player) {
		if (auto* cell = player->GetParentCell()) {
			actor->SetParentCell(cell);
		}
	}
	actor->Enable(true);
	actor->SetScale(1.0f);
	CMP_SetGhostLabel(actor, GhostLabel(pose).c_str());
	EnsureGhost3D(actor);
	if (!actor->Get3D()) {
		REX::WARN("Ghost {:08X} via {} has no 3D yet, will retry", actor->GetFormID(), path);
	} else {
		g_ghostReady.insert(pose.peerId);
		FreezeGhost(actor, pose.peerId);
		CMP_ApplyGhostAppearance(actor, pose.peerId);
		CMP_ApplyGhostInventory(actor, pose.peerId);
		REX::INFO("Ghost {:08X} 3D ready via {}", actor->GetFormID(), path);
	}
	SetGhostNote("");
	REX::INFO("Spawned ghost {:08X} for peer {} via {} at ({:.0f},{:.0f},{:.0f}) 3D={}",
		actor->GetFormID(), pose.peerId, path, pose.x, pose.y, pose.z, actor->Get3D() ? 1 : 0);
}

RE::Actor* SpawnGhostNative(const cmp::PlayerPose& pose)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* data = RE::TESDataHandler::GetSingleton();
	auto* base = CloneGhostBase(pose.peerId);
	if (!player || !data || !base) {
		return nullptr;
	}

	if (cmp::forbidden_actor_base(base->GetFormID())) {
		REX::ERROR("Refusing to place player/workshop ActorBase");
		SetGhostNote("refused forbidden ActorBase");
		return nullptr;
	}

	auto* cell = player->GetParentCell();
	if (!cell) {
		return nullptr;
	}

	const auto here = player->GetPosition();
	REX::INFO("CreateReferenceAtLocation begin base={:08X} peer={} cell={:08X} at ({:.0f},{:.0f},{:.0f})",
		base->GetFormID(),
		pose.peerId,
		cell->GetFormID(),
		here.x,
		here.y,
		here.z);
	RE::NEW_REFR_DATA spawn;
	spawn.location = here;
	spawn.direction = RE::NiPoint3{ 0.0f, 0.0f, pose.yaw };
	spawn.object = base;
	spawn.initializeScripts = false;
	spawn.forcePersist = false;
	spawn.clearStillLoadingFlag = true;
	spawn.initiallyDisabled = false;
	if (cell->IsInterior()) {
		spawn.interior = cell;
		spawn.world = nullptr;
	} else {
		spawn.interior = nullptr;
		spawn.world = cell->worldSpace;
	}

	const auto handle = data->CreateReferenceAtLocation(spawn);
	const auto ptr = handle.get();
	auto* actor = ptr ? ptr->As<RE::Actor>() : nullptr;
	if (!actor) {
		REX::WARN("CreateReferenceAtLocation did not yield an Actor");
		SetGhostNote("CreateReferenceAtLocation failed");
		return nullptr;
	}
	REX::INFO("CreateReferenceAtLocation ok {:08X}", actor->GetFormID());
	FinishGhostSetup(actor, pose, "CreateReferenceAtLocation");
	return actor;
}

RE::Actor* SpawnGhost(const cmp::PlayerPose& pose)
{
	return SpawnGhostNative(pose);
}

void MoveGhost(RE::Actor* actor, const cmp::PlayerPose& pose)
{
	if (!actor) {
		return;
	}
	if (!actor->Get3D()) {
		actor->SetPosition(RE::NiPoint3{ pose.x, pose.y, pose.z }, true);
		actor->SetHeading(pose.yaw);
		EnsureGhost3D(actor);
		return;
	}
	if (!g_ghostReady.contains(pose.peerId)) {
		g_ghostReady.insert(pose.peerId);
		FreezeGhost(actor, pose.peerId);
		CMP_ApplyGhostAppearance(actor, pose.peerId);
		CMP_ApplyGhostInventory(actor, pose.peerId);
		REX::INFO("Ghost {:08X} 3D attached, applied look", actor->GetFormID());
	} else {
		FreezeGhost(actor, pose.peerId);
	}

	using clock = std::chrono::steady_clock;
	const double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	double dt = 1.0 / 20.0;
	if (auto it = g_lastMoveSec.find(pose.peerId); it != g_lastMoveSec.end()) {
		dt = std::clamp(now - it->second, 1.0 / 120.0, 0.25);
	}
	g_lastMoveSec[pose.peerId] = now;

	const float poseHz = static_cast<float>(std::max(1, CMP_Session().settings.poseHz));
	const float halfRtt = 0.5f / poseHz;
	const float tx = pose.x + pose.vx * halfRtt;
	const float ty = pose.y + pose.vy * halfRtt;
	const float tz = pose.z;

	const auto cur = actor->GetPosition();
	constexpr float snap = 512.0f;
	float dx = tx - cur.x;
	float dy = ty - cur.y;
	float dz = tz - cur.z;
	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
	RE::NiPoint3 next;
	if (dist > snap || dist < 0.01f) {
		next = RE::NiPoint3{ tx, ty, tz };
	} else {
		const float speed = std::max(pose.speed, 40.0f);
		const float step = std::min(dist, speed * static_cast<float>(dt) * 1.25f);
		const float s = step / dist;
		next = RE::NiPoint3{
			cur.x + dx * s,
			cur.y + dy * s,
			cur.z + dz * s
		};
	}
	actor->SetPosition(next, true);
	actor->SetHeading(pose.yaw);
	CMP_ApplyGhostPuppet(actor, pose);
}

}  // namespace

bool CMP_IsPaintableGhostBase(RE::TESNPC* npc)
{
	if (!npc || cmp::forbidden_actor_base(npc->GetFormID())) {
		return false;
	}
	std::lock_guard lock(g_cloneMutex);
	return g_cloneFormIds.contains(npc->GetFormID());
}

void CMP_SetGhostLabel(RE::Actor* actor, const char* name)
{
	if (!actor || !name || !name[0]) {
		return;
	}
	if (actor->extraList) {
		actor->extraList->SetOverrideName(name);
	}
	if (auto* npc = actor->GetNPC()) {
		RE::TESFullName::SetFullName(*npc, name);
	}
}

int CMP_CountGhostsWith3D()
{
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	int n = 0;
	for (const auto& [id, handle] : s.ghosts) {
		if (const auto ptr = handle.get()) {
			if (auto* actor = ptr->As<RE::Actor>(); actor && actor->Get3D()) {
				++n;
			}
		}
	}
	return n;
}

void CMP_DespawnGhosts()
{
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	for (auto& [id, handle] : s.ghosts) {
		if (const auto ptr = handle.get()) {
			ptr->Disable();
		}
	}
	s.ghosts.clear();
	s.ghostNames.clear();
	s.lastGhostNote.clear();
	ClearAllClones();
}

void CMP_ApplyGhosts()
{
	CMP_CrashNote("ghosts");
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell()) {
		return;
	}

	const auto loc = PlayerLocationForm();
	auto& s = CMP_Session();

	std::unordered_map<std::uint32_t, cmp::PlayerPose> poses;
	{
		std::lock_guard lock(s.mutex);
		poses = s.latestPose;
	}

	std::unordered_set<std::uint32_t> live;
	if (!s.settings.ghostSpawn) {
		SetGhostNote("Ghost.Spawn=0 (join only, no bodies)");
	}

	for (const auto& [peer, pose] : poses) {
		if (peer == s.myPeerId) {
			continue;
		}
		if (!s.settings.ghostSpawn) {
			continue;
		}
		if (pose.locationFormId != 0 && loc != 0 && pose.locationFormId != loc) {
			SetGhostNote("remote in another cell (same-cell ghosts only)");
			continue;
		}
		live.insert(peer);

		RE::Actor* actor = nullptr;
		{
			std::lock_guard lock(s.mutex);
			auto it = s.ghosts.find(peer);
			if (it != s.ghosts.end()) {
				if (const auto ptr = it->second.get()) {
					actor = ptr->As<RE::Actor>();
				}
			}
		}
		if (!actor) {
			actor = SpawnGhost(pose);
			if (!actor) {
				continue;
			}
			std::lock_guard lock(s.mutex);
			s.ghosts[peer] = actor->GetHandle();
		}
		MoveGhost(actor, pose);
	}

	std::lock_guard lock(s.mutex);
	for (auto it = s.ghosts.begin(); it != s.ghosts.end();) {
		if (!live.contains(it->first)) {
			if (const auto ptr = it->second.get()) {
				ptr->Disable();
			}
			DropPeerClone(it->first);
			it = s.ghosts.erase(it);
		} else {
			++it;
		}
	}
}
