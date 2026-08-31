#include "pch.h"
#include "cmp.h"

void CMP_OnGameReady()
{
	auto& s = CMP_Session();
	if (!s.probedForms) {
		s.probedForms = true;
		CMP_ProbeForms();
	}
	if (s.menuJoin || CMP_MenuJoinPending()) {
		return;
	}
	if (s.settings.autoJoin && !s.joined) {
		CMP_Join(s.settings.host, s.settings.port);
	}
}

void CMP_OnPreLoad()
{
	CMP_Leave();
	CMP_DespawnGhosts();
}

F4SE_PLUGIN_PRELOAD(const F4SE::PreLoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, { .log = true, .logName = "CommonwealthMP" });
	CMP_InstallCrashHandler();
	REX::INFO("CommonwealthMP preload");
	return true;
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, { .log = true, .logName = "CommonwealthMP" });
	CMP_InstallCrashHandler();
	REX::INFO("CommonwealthMP 0.5.7 load (F4SE {} runtime plugin for 1.11.x / Address Library) {}", F4SE::GetF4SEVersion(), CMP_VersionStamp());

	CMP_LoadSettings();
	CMP_RegisterConsole();
	REX::INFO("{}", CMP_VersionStamp());

	if (auto* tasks = F4SE::GetTaskInterface()) {
		tasks->AddTaskPermanent([]() {
			CMP_WatchQuit();
			CMP_CrashNote("task");
			CMP_MenuTick();
			CMP_NetPoll();
			CMP_SendLocalPose();
			CMP_ApplyGhosts();
			CMP_PointerTick();
		});
	} else {
		REX::ERROR("No F4SE task interface");
	}

	if (auto* msg = F4SE::GetMessagingInterface()) {
		msg->RegisterListener([](F4SE::MessagingInterface::Message* m) {
			if (!m) {
				return;
			}
			switch (m->type) {
			case F4SE::MessagingInterface::kGameDataReady:
				CMP_InstallMenu();
				CMP_OnGameReady();
				break;
			case F4SE::MessagingInterface::kNewGame:
				CMP_OnGameReady();
				CMP_MenuOnNewGame();
				break;
			case F4SE::MessagingInterface::kPostLoadGame:
				CMP_OnGameReady();
				break;
			case F4SE::MessagingInterface::kPreLoadGame:
				CMP_OnPreLoad();
				break;
			default:
				break;
			}
		});
	}

	REX::INFO("Loaded CommonwealthMP {}", CMP_VersionStamp());
	return true;
}
