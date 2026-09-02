#include "pch.h"
#include "menu/internal.h"
#include "menu.h"

#include "net.h"
#include "presence.h"
#include "session.h"
#include "ui_pose.h"

#include <cstring>
#include <string>

#if __has_include("cmp_build_meta.h")
#include "cmp_build_meta.h"
#endif
#ifndef CMP_BUILD_STAMP
#define CMP_BUILD_STAMP __DATE__ " " __TIME__
#endif
#ifndef CMP_GIT_VERSION
#define CMP_GIT_VERSION "unknown"
#endif

namespace cmp_menu {

Flow g_flow{ Flow::Idle };
double g_flowAt{ 0.0 };
bool g_sinkReady{ false };
int g_stampWait{ 0 };
MenuForm g_menuForm{ MenuForm::None };
bool g_menuPopupQueued{ false };
bool g_pendingMenuHostJoin{ false };
bool g_hostSavesRefreshed{ false };
int g_hostSaveSelection{ -1 };
std::vector<HostSaveRow> g_hostSaves;

bool g_looksOpen{ false };
bool g_specialOpen{ false };
JoinContext g_joinContext{ JoinContext::TitleMenu };
JoinContext g_hostContext{ JoinContext::TitleMenu };
std::string g_stamp;
std::string g_statusStorage;
char g_joinHost[256]{};
char g_joinPort[16]{};
char g_joinName[64]{};
char g_joinPassword[16]{};

class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
	{
		const auto name = a_event.menuName.c_str();
		if (std::strcmp(name, "MainMenu") == 0) {
			if (a_event.opening) {
				g_stampWait = 0;
				g_menuForm = MenuForm::None;
			} else {
				CloseMenuForm();
			}
		} else if (std::strcmp(name, "LooksMenu") == 0) {
			g_looksOpen = a_event.opening;
		} else if (std::strcmp(name, "SPECIALMenu") == 0) {
			g_specialOpen = a_event.opening;
		}
		CMP_OnMenuOpenClose(name, a_event.opening);
		return RE::BSEventNotifyControl::kContinue;
	}
};

MenuSink g_sink;

}  // namespace cmp_menu

std::string CMP_VersionStamp()
{
	return std::string("CMP ") + F4SE::GetPluginVersion().string() + "  " + CMP_BUILD_STAMP + "  " + CMP_GIT_VERSION;
}

std::string CMP_MenuPresencePhase(bool& menuActive)
{
	using namespace cmp_menu;
	menuActive = g_flow != Flow::Idle && g_flow != Flow::Done && g_flow != Flow::Failed;
	switch (g_flow) {
	case Flow::Querying:
		return "Querying server";
	case Flow::StartingGame:
	case Flow::WaitWorld:
		return "Starting game";
	case Flow::SkipIntro:
	case Flow::WaitSkip:
		return "Skipping intro";
	case Flow::Looks:
	case Flow::WaitLooks:
		return "Creating character";
	case Flow::Special:
	case Flow::WaitSpecial:
		return "Setting SPECIAL";
	case Flow::Strip:
		return "Preparing to join";
	case Flow::Joining:
		return "Joining server";
	default:
		return {};
	}
}

bool CMP_MenuJoinPending()
{
	using namespace cmp_menu;
	return g_flow != Flow::Idle && g_flow != Flow::Done && g_flow != Flow::Failed;
}

bool CMP_MenuFormOpen()
{
	using namespace cmp_menu;
	return g_menuForm != MenuForm::None;
}

bool CMP_MenuHostJoinPending()
{
	using namespace cmp_menu;
	return g_pendingMenuHostJoin;
}

void CMP_MenuHostJoinAfterLoad()
{
	using namespace cmp_menu;
	if (!g_pendingMenuHostJoin) {
		return;
	}
	g_pendingMenuHostJoin = false;
	auto& s = CMP_Session();
	if (CMP_Join("127.0.0.1", s.settings.port)) {
		SetStatus("connected as host");
	} else {
		SetStatus("host join failed");
	}
}

void CMP_StripLocalGear()
{
	RE::Console::ExecuteCommand("player.removeallitems");
	RE::Console::ExecuteCommand("player.additem 21B3B 1");
	RE::Console::ExecuteCommand("player.equipitem 21B3B");
	REX::INFO("stripped local gear, pip-boy kept");
}

void CMP_InstallMenu()
{
	using namespace cmp_menu;
	if (g_sinkReady) {
		return;
	}
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}
	ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(&g_sink);
	g_sinkReady = true;
	REX::INFO("MainMenu sink ready {}", CMP_VersionStamp());
}

void CMP_MenuTick()
{
	using namespace cmp_menu;
	if (!g_sinkReady) {
		CMP_InstallMenu();
	}
	TryStampMainMenuVersion();
	TickFlow();
}

void CMP_MenuOpenJoin(bool titleMenu)
{
	using namespace cmp_menu;
	OpenJoinFlow(titleMenu ? JoinContext::TitleMenu : JoinContext::InGame);
}

void CMP_MenuOpenHost(bool titleMenu)
{
	using namespace cmp_menu;
	OpenHostFlow(titleMenu ? JoinContext::TitleMenu : JoinContext::InGame);
}

void CMP_MenuHost()
{
	using namespace cmp_menu;
	OpenHostFlow(JoinContext::InGame);
}

void CMP_MenuDisconnect()
{
	using namespace cmp_menu;
	DisconnectSession();
}

bool CMP_MenuIsConnected()
{
	return CMP_Session().net.joined;
}

void CMP_MenuOnNewGame()
{
	using namespace cmp_menu;
	if (g_flow == Flow::StartingGame || CMP_Session().menu.menuJoin) {
		SetFlow(Flow::WaitWorld);
		SetStatus("new game loaded, waiting for player");
	}
}
