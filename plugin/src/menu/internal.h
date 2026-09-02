#pragma once

#include "session.h"
#include "cmp_protocol.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace RE {
class IMenu;
class MainMenu;
}

namespace Scaleform {
namespace GFx {
class Value;
}
}

namespace cmp_menu {

enum class Flow : std::uint8_t {
	Idle,
	Querying,
	StartingGame,
	WaitWorld,
	SkipIntro,
	WaitSkip,
	Looks,
	WaitLooks,
	Special,
	WaitSpecial,
	Strip,
	Joining,
	Done,
	Failed
};

enum class MenuForm : std::uint8_t {
	None,
	Join,
	Host
};

enum class JoinContext : std::uint8_t {
	TitleMenu,
	InGame
};

struct HostSaveRow {
	int listIndex{ -1 };
	std::string label;
};

extern Flow g_flow;
extern double g_flowAt;
extern bool g_sinkReady;
extern int g_stampWait;
extern MenuForm g_menuForm;
extern bool g_menuPopupQueued;
extern bool g_pendingMenuHostJoin;
extern bool g_hostSavesRefreshed;
extern int g_hostSaveSelection;
extern std::vector<HostSaveRow> g_hostSaves;

constexpr const char* kCmpVersionMarker = "CMP ";
constexpr int kVersionStampWaitMax = 180;
constexpr double kVersionMarginX = 16.0;
constexpr double kVersionMarginY = 14.0;
constexpr double kVersionFieldWidth = 440.0;
constexpr double kVersionLineHeight = 18.0;

extern bool g_looksOpen;
extern bool g_specialOpen;
extern JoinContext g_joinContext;
extern JoinContext g_hostContext;
extern std::string g_stamp;
extern std::string g_statusStorage;
extern char g_joinHost[256];
extern char g_joinPort[16];
extern char g_joinName[64];
extern char g_joinPassword[16];

double NowSec();
void SetFlow(Flow f);
bool TimedOut(double seconds);
RE::MainMenu* MainMenuPtr();
void ClosePauseMenu();
bool MenuMovieReady(RE::IMenu* menu);
bool MainMenuReady(RE::MainMenu* menu);
bool GfxMemberString(const Scaleform::GFx::Value& a_obj, const char* a_member, std::string& a_out);
bool IsPauseMainMenu(RE::MainMenu* a_menu);
bool VersionHtmlHasVanilla(std::string_view a_html);
std::string TrimCopyLocal(std::string_view a_text);
std::string StripHtmlToText(std::string a_html);
std::string FindLineContaining(std::string_view a_text, std::string_view a_needle);
std::string DefaultGameVersionLine();
std::string DefaultF4seVersionLine();
std::string BuildVersionText();
bool VersionTextLooksCorrect(std::string_view a_text);
void GetStageSize(RE::MainMenu* a_menu, double& a_width, double& a_height);
void EnsureVersionOnRoot(RE::MainMenu* a_menu, Scaleform::GFx::Value& a_rootObj);
void AnchorVersionBottomRight(RE::MainMenu* a_menu);
void SetStatus(const std::string& text);
void StampVersion(RE::MainMenu* menu);
void BeginChargen();
void OnQueryFinished(const SessionQueryResult& r);
void CopyJoinField(char* dest, std::size_t destSize, const char* src);
void CloseMenuForm();
std::string TrimCopy(std::string s);
void SyncJoinFieldsFromSettings();
bool ParseJoinAddress(std::string& hostOut, std::uint16_t& portOut, std::string& err);
bool ParsePortField(std::uint16_t& portOut, std::string& err);
void RefreshHostSaveList();
void LoadSaveFromTitleMenu(int saveIndex);
void ConfirmHost();
void ConfirmJoin();
void OpenHostFlow(JoinContext ctx);
void DisconnectSession();
void OpenJoinFlow(JoinContext ctx);
void TryStampMainMenuVersion();
void SkipIntro();
void OpenLooks();
void OpenSpecial();
void CopyChargenName();
void TickFlow();

}  // namespace cmp_menu
