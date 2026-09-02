#include "pch.h"
#include "menu/internal.h"

#include "net.h"
#include "presence.h"
#include "menu.h"

#include "Scaleform/G/GFx_Value.h"

#include <chrono>

namespace cmp_menu {

using GFxValue = Scaleform::GFx::Value;

double NowSec()
{
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

void SetFlow(Flow f)
{
	g_flow = f;
	g_flowAt = NowSec();
	CMP_Presence_Invalidate();
}

bool TimedOut(double seconds)
{
	return NowSec() - g_flowAt > seconds;
}

RE::MainMenu* MainMenuPtr()
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return nullptr;
	}
	const auto ptr = ui->GetMenu<RE::MainMenu>();
	return ptr.get();
}

void ClosePauseMenu()
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui || !ui->GetMenuOpen("PauseMenu"sv)) {
		return;
	}
	if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
		queue->AddMessage("PauseMenu"sv, RE::UI_MESSAGE_TYPE::kHide);
	}
}

bool MenuMovieReady(RE::IMenu* menu)
{
	return menu && menu->uiMovie && menu->menuObj.IsObject() && menu->hasDoneFirstAdvanceMovie;
}

bool MainMenuReady(RE::MainMenu* menu)
{
	if (!MenuMovieReady(menu)) {
		return false;
	}
	return menu->gameDataReady && menu->GetIsMenuReady();
}

bool IsPauseMainMenu(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}
	GFxValue pauseMode;
	if (!a_menu->uiMovie->GetVariable(&pauseMode, "root.Menu_mc.PauseMode")) {
		return false;
	}
	return pauseMode.IsBoolean() && pauseMode.GetBoolean();
}

void SetStatus(const std::string& text)
{
	g_statusStorage = text;
	CMP_Session().lastStatus = text;
	REX::INFO("menu {}", text);
	if (auto* mm = MainMenuPtr(); mm && mm->confirmText) {
		mm->confirmText->SetMember("text", GFxValue(g_statusStorage.c_str()));
	}
	RE::SendHUDMessage::ShowHUDMessage(text.c_str(), "", false, false);
}

void OnQueryFinished(const SessionQueryResult& r)
{
	if (!r.ok) {
		SetFlow(Flow::Failed);
		std::string err = r.error.empty() ? "join failed" : r.error;
		if (r.info.serverName[0]) {
			err = std::string(r.info.serverName) + ": " + err;
		}
		SetStatus(err);
		if (g_joinContext == JoinContext::TitleMenu) {
			if (auto* mgr = RE::MessageMenuManager::GetSingleton()) {
				mgr->Create(
					"JOIN",
					g_statusStorage.c_str(),
					nullptr,
					RE::WARNING_TYPES::kMenus,
					"OK",
					nullptr,
					nullptr,
					nullptr,
					true);
			}
		} else {
			RE::SendHUDMessage::ShowHUDMessage(err.c_str(), "", false, false);
		}
		SetFlow(Flow::Idle);
		CMP_Session().menu.menuJoin = false;
		return;
	}
	std::string ok = "host ok";
	if (r.info.serverName[0]) {
		ok = r.info.serverName;
	}
	ok += " (" + std::to_string(r.info.clientCount) + "/" + std::to_string(r.info.maxPlayers) + ")";
	if (r.info.motd[0]) {
		ok += "\n";
		ok += r.info.motd;
	}
	SetStatus(ok);
	if (g_joinContext == JoinContext::InGame) {
		auto& s = CMP_Session();
		if (CMP_Join(s.settings.host, s.settings.port)) {
			SetFlow(Flow::Done);
			SetStatus(s.net.isHost ? "Connected as host" : "Connected as guest");
			ClosePauseMenu();
		} else {
			SetFlow(Flow::Failed);
			SetStatus("join failed after query");
		}
		return;
	}
	BeginChargen();
}

void TickFlow()
{
	auto& s = CMP_Session();
	switch (g_flow) {
	case Flow::Idle:
	case Flow::Done:
	case Flow::Failed:
		break;
	case Flow::Querying: {
		SessionQueryResult r;
		if (CMP_QueryPoll(r)) {
			OnQueryFinished(r);
		}
		break;
	}
	case Flow::StartingGame:
		if (TimedOut(30.0)) {
			SetFlow(Flow::Failed);
			SetStatus("new game did not start");
			s.menu.menuJoin = false;
		}
		break;
	case Flow::WaitWorld: {
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && player->GetParentCell()) {
			SetFlow(Flow::SkipIntro);
		} else if (TimedOut(90.0)) {
			SetFlow(Flow::Failed);
			SetStatus("timed out waiting for world");
			s.menu.menuJoin = false;
		}
		break;
	}
	case Flow::SkipIntro:
		SkipIntro();
		SetFlow(Flow::WaitSkip);
		break;
	case Flow::WaitSkip:
		if (CMP_PlayerInCommonwealth()) {
			if (g_looksOpen) {
				SetFlow(Flow::WaitLooks);
			} else {
				SetFlow(Flow::Looks);
			}
		} else if (TimedOut(20.0)) {
			CMP_EnsureCommonwealthExterior();
		}
		if (TimedOut(60.0)) {
			SetFlow(Flow::Failed);
			SetStatus("could not reach Commonwealth");
			s.menu.menuJoin = false;
		}
		break;
	case Flow::Looks:
		OpenLooks();
		SetFlow(Flow::WaitLooks);
		break;
	case Flow::WaitLooks:
		if (!g_looksOpen && NowSec() - g_flowAt > 1.0) {
			SetFlow(Flow::Special);
		} else if (TimedOut(600.0)) {
			SetFlow(Flow::Special);
		}
		break;
	case Flow::Special:
		OpenSpecial();
		SetFlow(Flow::WaitSpecial);
		break;
	case Flow::WaitSpecial:
		if (!g_specialOpen && NowSec() - g_flowAt > 1.0) {
			SetFlow(Flow::Strip);
		} else if (TimedOut(600.0)) {
			SetFlow(Flow::Strip);
		}
		break;
	case Flow::Strip:
		CopyChargenName();
		CMP_StripLocalGear();
		if (!CMP_PlayerInCommonwealth()) {
			CMP_EnsureCommonwealthExterior();
		}
		SetFlow(Flow::Joining);
		break;
	case Flow::Joining:
		if (!CMP_PlayerInCommonwealth()) {
			if (TimedOut(20.0)) {
				SetFlow(Flow::Failed);
				SetStatus("not in Commonwealth for Hello");
				s.menu.menuJoin = false;
			}
			break;
		}
		if (CMP_Join(s.settings.host, s.settings.port, cmp::kHelloFlagRequireHost)) {
			s.menu.menuJoin = false;
			SetFlow(Flow::Done);
			SetStatus("joined host");
		} else {
			SetFlow(Flow::Failed);
			s.menu.menuJoin = false;
		}
		break;
	default:
		break;
	}
}

}  // namespace cmp_menu
