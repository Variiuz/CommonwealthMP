#include "pch.h"
#include "session.h"
#include "lifecycle.h"
#include "config.h"
#include "net.h"
#include "menu.h"
#include "scaleform_menu.h"
#include "ghost.h"
#include "actors.h"
#include "combat.h"
#include "console.h"
#include "crash.h"
#include "indicators.h"
#include "overlay.h"
#include "pointer.h"
#include "presence.h"
#include "input_hook.h"
#include "render_hook.h"

void CMP_OnGameReady()
{
	auto& s = CMP_Session();
	CMP_RefreshPlayerNameFromSteam(false);
	if (!s.net.probedForms) {
		s.net.probedForms = true;
		CMP_ProbeForms();
	}
	if (s.menu.menuJoin || CMP_MenuJoinPending()) {
		return;
	}
	if (CMP_MenuHostJoinPending()) {
		CMP_MenuHostJoinAfterLoad();
		return;
	}
	if (s.settings.autoJoin && !s.net.joined) {
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
	REX::INFO("CommonwealthMP {} load (F4SE {}) {}", F4SE::GetPluginVersion().string(), F4SE::GetF4SEVersion(), CMP_VersionStamp());

	CMP_LoadSettings();
	CMP_RegisterConsole();
	CMP_Presence_Init();
	CMP_InstallScaleformMenu();

	if (auto* tasks = F4SE::GetTaskInterface()) {
		tasks->AddTaskPermanent([]() {
			CMP_WatchQuit();
			CMP_CrashNote("task");
			CMP_MenuTick();
			CMP_ScaleformMenuTick();
			CMP_NetPoll();
			CMP_SendLocalPose();
			CMP_ApplyGhosts();
			CMP_ApplyHostActors();
			CMP_IndicatorsTick();
			CMP_PointerTick();
			CMP_Overlay_Tick();
			CMP_Presence_Tick();
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
				CMP_RegisterScaleformMenuEvents();
				CMP_InstallCombat();
				CMP_Presence_OnGameReady();
				CMP_OnGameReady();
				break;
			case F4SE::MessagingInterface::kNewGame:
				CMP_Presence_OnGameReady();
				CMP_OnGameReady();
				CMP_MenuOnNewGame();
				break;
			case F4SE::MessagingInterface::kPostLoadGame:
				CMP_Presence_OnGameReady();
				CMP_OnGameReady();
				break;
			case F4SE::MessagingInterface::kPreLoadGame:
				CMP_Presence_OnPreLoad();
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
