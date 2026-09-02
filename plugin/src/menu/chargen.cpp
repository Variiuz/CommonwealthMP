#include "pch.h"
#include "menu/internal.h"

#include "net.h"
#include "session.h"

namespace cmp_menu {

void BeginChargen()
{
	auto& s = CMP_Session();
	s.menu.menuJoin = true;
	s.menu.joinFlags = cmp::kHelloFlagRequireHost;
	SetFlow(Flow::StartingGame);
	SetStatus("starting new game for join");
	if (auto* mm = MainMenuPtr()) {
		mm->queueStartNewGame = true;
		mm->mainMenuExitCondition = RE::MainMenu::MAIN_MENU_EXIT_CONDITION::kNewGame;
	}
}

void SkipIntro()
{
	RE::Console::ExecuteCommand("StopQuest MQ101");
	RE::Console::ExecuteCommand("StopQuest MQ102");
	RE::Console::ExecuteCommand("enableplayercontrols");
	CMP_EnsureCommonwealthExterior();
	SetStatus("skipping vault, moving to Commonwealth");
}

void OpenLooks()
{
	RE::Console::ExecuteCommand("showlooksmenu player");
	SetStatus("character looks");
}

void OpenSpecial()
{
	RE::Console::ExecuteCommand("ShowSPECIALMenu");
	SetStatus("SPECIAL and name");
}

void CopyChargenName()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}
	const char* name = player->GetDisplayFullName();
	if (name && name[0]) {
		CMP_Session().settings.playerName = name;
		REX::INFO("chargen name {}", name);
	}
}

}  // namespace cmp_menu
