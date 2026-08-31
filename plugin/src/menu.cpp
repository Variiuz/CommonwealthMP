#include "pch.h"
#include "cmp.h"

#include "Scaleform/G/GFx_FunctionHandler.h"
#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_Value.h"

#include <chrono>
#include <cstring>
#include <string>

#ifndef CMP_BUILD_STAMP
#define CMP_BUILD_STAMP __DATE__ " " __TIME__
#endif

namespace {

using GFxValue = Scaleform::GFx::Value;
using GFxMovie = Scaleform::GFx::Movie;

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

Flow g_flow{ Flow::Idle };
double g_flowAt{ 0.0 };
bool g_sinkReady{ false };
bool g_stamped{ false };
bool g_joinInjected{ false };
bool g_overlayAttached{ false };
bool g_joinPrompted{ false };
bool g_joinPanelOpen{ false };
bool g_looksOpen{ false };
bool g_specialOpen{ false };
std::int32_t g_joinListIndex{ -1 };
std::string g_stamp;
std::string g_joinLabel{ "JOIN SERVER" };
std::string g_statusStorage;
GFxValue g_joinPanel;
GFxValue g_hostField;
GFxValue g_portField;
GFxValue g_joinList;

double NowSec()
{
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

void SetFlow(Flow f)
{
	g_flow = f;
	g_flowAt = NowSec();
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

bool PushListItem(GFxValue& list)
{
	GFxValue entries;
	if (!list.GetMember("entryList", &entries) || !entries.IsArray()) {
		return false;
	}
	const auto n = entries.GetArraySize();
	GFxMovie* movie = nullptr;
	if (auto* mm = MainMenuPtr(); mm) {
		movie = mm->uiMovie.get();
	}
	if (!movie) {
		return false;
	}
	GFxValue item;
	movie->CreateObject(&item);
	if (!item.IsObject()) {
		return false;
	}
	item.SetMember("text", GFxValue(g_joinLabel.c_str()));
	item.SetMember("label", GFxValue(g_joinLabel.c_str()));
	item.SetMember("index", GFxValue(static_cast<std::int32_t>(n)));
	item.SetMember("disabled", GFxValue(false));
	if (!entries.PushBack(item)) {
		GFxValue pushed;
		if (!entries.Invoke("push", &pushed, &item, 1)) {
			return false;
		}
	}
	list.Invoke("InvalidateData");
	list.Invoke("invalidateData");
	g_joinListIndex = static_cast<std::int32_t>(n);
	g_joinList = list;
	REX::INFO("MainMenu JOIN SERVER list index={}", n);
	return true;
}

bool TryInjectJoinRow(RE::IMenu* menu)
{
	if (!menu || !menu->uiMovie || g_joinInjected) {
		return g_joinInjected;
	}
	static const char* kLists[] = {
		"_root.Menu_mc.MainPanel_mc.List_mc",
		"_root.Menu_mc.MainPanel_mc.MainList_mc",
		"_root.Menu_mc.List_mc",
		"_root.MainMenu_mc.MainPanel_mc.List_mc",
		"Menu_mc.MainPanel_mc.List_mc"
	};
	for (const char* path : kLists) {
		GFxValue list;
		if (menu->uiMovie->GetVariable(&list, path) && list.IsObject()) {
			if (PushListItem(list)) {
				g_joinInjected = true;
				return true;
			}
		}
	}
	if (menu->menuObj.IsObject()) {
		GFxValue list;
		if (menu->menuObj.GetMember("List_mc", &list) && list.IsObject() && PushListItem(list)) {
			g_joinInjected = true;
			return true;
		}
		if (menu->menuObj.GetMember("MainList_mc", &list) && list.IsObject() && PushListItem(list)) {
			g_joinInjected = true;
			return true;
		}
	}
	return false;
}

void StampVersion(RE::IMenu* menu)
{
	g_stamp = CMP_VersionStamp();
	if (!menu || !menu->uiMovie) {
		g_stamped = true;
		return;
	}
	GFxValue root;
	if (!menu->uiMovie->GetVariable(&root, "_root") || !root.IsObject()) {
		g_stamped = true;
		return;
	}
	GFxValue tf;
	menu->uiMovie->CreateObject(&tf, "flash.text.TextField");
	if (!(tf.IsObject() || tf.IsDisplayObject())) {
		g_stamped = true;
		return;
	}
	// Stage is typically 1280x720; pin bright stamp to the lower-right corner.
	tf.SetMember("x", GFxValue(920.0));
	tf.SetMember("y", GFxValue(686.0));
	tf.SetMember("width", GFxValue(340.0));
	tf.SetMember("height", GFxValue(28.0));
	tf.SetMember("selectable", GFxValue(false));
	tf.SetMember("mouseEnabled", GFxValue(false));
	tf.SetMember("textColor", GFxValue(static_cast<double>(0xF5F0E6)));
	const std::string html = std::string("<p align='right'><font color='#F5F0E6' size='18'><b>")
		+ g_stamp + "</b></font></p>";
	tf.SetMember("htmlText", GFxValue(html.c_str()));
	root.Invoke("addChild", nullptr, &tf, 1);
	g_stamped = true;
	REX::INFO("MainMenu version stamp overlay {}", g_stamp);
}

void BeginChargen()
{
	auto& s = CMP_Session();
	s.menuJoin = true;
	s.joinFlags = cmp::kHelloFlagRequireHost;
	SetFlow(Flow::StartingGame);
	SetStatus("starting new game for join");
	if (auto* mm = MainMenuPtr()) {
		mm->queueStartNewGame = true;
		mm->mainMenuExitCondition = RE::MainMenu::MAIN_MENU_EXIT_CONDITION::kNewGame;
	}
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
		if (auto* mgr = RE::MessageMenuManager::GetSingleton()) {
			mgr->Create(
				"JOIN SERVER",
				g_statusStorage.c_str(),
				nullptr,
				RE::WARNING_TYPES::kMenus,
				"OK",
				nullptr,
				nullptr,
				nullptr,
				true);
		}
		SetFlow(Flow::Idle);
		CMP_Session().menuJoin = false;
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
	BeginChargen();
}

class JoinBoxCallback : public RE::IMessageBoxCallback
{
public:
	void operator()(std::uint8_t a_buttonIdx) override
	{
		if (a_buttonIdx != 0) {
			SetFlow(Flow::Idle);
			SetStatus("join cancelled");
			return;
		}
		auto& s = CMP_Session();
		SetFlow(Flow::Querying);
		SetStatus("querying " + s.settings.host + ":" + std::to_string(s.settings.port));
		CMP_QueryStart(s.settings.host, s.settings.port);
	}
};

void CloseJoinPanel()
{
	if (!g_joinPanelOpen) {
		return;
	}
	if (auto* mm = MainMenuPtr(); mm && mm->uiMovie && g_joinPanel.IsObject()) {
		GFxValue root;
		if (mm->uiMovie->GetVariable(&root, "_root") && root.IsObject()) {
			root.Invoke("removeChild", nullptr, &g_joinPanel, 1);
		}
	}
	g_joinPanel = nullptr;
	g_hostField = nullptr;
	g_portField = nullptr;
	g_joinPanelOpen = false;
	g_joinPrompted = false;
}

std::string GfxText(GFxValue& field)
{
	GFxValue text;
	if (!field.IsObject() || !field.GetMember("text", &text) || !text.IsString() || !text.GetString()) {
		return {};
	}
	return text.GetString();
}

std::string TrimCopy(std::string s)
{
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		s.erase(s.begin());
	}
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
		s.pop_back();
	}
	return s;
}

bool ParseJoinAddress(std::string& hostOut, std::uint16_t& portOut, std::string& err)
{
	auto host = TrimCopy(GfxText(g_hostField));
	auto portStr = TrimCopy(GfxText(g_portField));
	if (host.empty()) {
		err = "host is empty";
		return false;
	}
	if (host.size() > 255) {
		err = "host too long";
		return false;
	}
	int port = cmp::kDefaultPort;
	try {
		port = std::stoi(portStr.empty() ? std::to_string(cmp::kDefaultPort) : portStr);
	} catch (...) {
		err = "port must be a number";
		return false;
	}
	if (port < 1 || port > 65535) {
		err = "port must be 1-65535";
		return false;
	}
	hostOut = std::move(host);
	portOut = static_cast<std::uint16_t>(port);
	return true;
}

void MakeLabel(GFxMovie* movie, GFxValue& parent, const char* text, double x, double y, double w = 520.0)
{
	GFxValue tf;
	movie->CreateObject(&tf, "flash.text.TextField");
	if (!(tf.IsObject() || tf.IsDisplayObject())) {
		return;
	}
	tf.SetMember("x", GFxValue(x));
	tf.SetMember("y", GFxValue(y));
	tf.SetMember("width", GFxValue(w));
	tf.SetMember("height", GFxValue(24.0));
	tf.SetMember("selectable", GFxValue(false));
	tf.SetMember("mouseEnabled", GFxValue(false));
	tf.SetMember("textColor", GFxValue(static_cast<double>(0xF5F0E6)));
	const std::string html = std::string("<font color='#F5F0E6' size='16'>") + text + "</font>";
	tf.SetMember("htmlText", GFxValue(html.c_str()));
	parent.Invoke("addChild", nullptr, &tf, 1);
}

void MakeInput(GFxMovie* movie, GFxValue& parent, GFxValue& out, const char* text, double x, double y, double w)
{
	movie->CreateObject(&out, "flash.text.TextField");
	if (!(out.IsObject() || out.IsDisplayObject())) {
		return;
	}
	out.SetMember("x", GFxValue(x));
	out.SetMember("y", GFxValue(y));
	out.SetMember("width", GFxValue(w));
	out.SetMember("height", GFxValue(28.0));
	out.SetMember("type", GFxValue("input"));
	out.SetMember("border", GFxValue(true));
	out.SetMember("background", GFxValue(true));
	out.SetMember("backgroundColor", GFxValue(static_cast<double>(0x141414)));
	out.SetMember("borderColor", GFxValue(static_cast<double>(0xC4A574)));
	out.SetMember("textColor", GFxValue(static_cast<double>(0xF5F0E6)));
	out.SetMember("selectable", GFxValue(true));
	out.SetMember("text", GFxValue(text));
	parent.Invoke("addChild", nullptr, &out, 1);
}

void MakeButton(GFxMovie* movie, GFxValue& parent, const char* label, double x, double y, Scaleform::GFx::FunctionHandler* fn)
{
	GFxValue tf;
	movie->CreateObject(&tf, "flash.text.TextField");
	if (!(tf.IsObject() || tf.IsDisplayObject())) {
		return;
	}
	tf.SetMember("x", GFxValue(x));
	tf.SetMember("y", GFxValue(y));
	tf.SetMember("width", GFxValue(160.0));
	tf.SetMember("height", GFxValue(30.0));
	tf.SetMember("border", GFxValue(true));
	tf.SetMember("background", GFxValue(true));
	tf.SetMember("backgroundColor", GFxValue(static_cast<double>(0x3A2F24)));
	tf.SetMember("borderColor", GFxValue(static_cast<double>(0xE0C48A)));
	tf.SetMember("selectable", GFxValue(false));
	const std::string html = std::string("<p align='center'><font color='#F5F0E6' size='16'><b>")
		+ label + "</b></font></p>";
	tf.SetMember("htmlText", GFxValue(html.c_str()));
	GFxValue handler;
	movie->CreateFunction(&handler, fn);
	GFxValue click("click");
	GFxValue args[2]{ click, handler };
	tf.Invoke("addEventListener", nullptr, args, 2);
	tf.SetMember("onPress", handler);
	parent.Invoke("addChild", nullptr, &tf, 1);
}

class JoinConfirmFn;
class JoinCancelFn;
class OpenJoinFn;
class JoinListPressFn;

JoinConfirmFn* g_joinConfirmFn = nullptr;
JoinCancelFn* g_joinCancelFn = nullptr;
OpenJoinFn* g_openJoinFn = nullptr;
JoinListPressFn* g_joinListPressFn = nullptr;

void OpenJoinPanel();

class JoinConfirmFn : public Scaleform::GFx::FunctionHandler
{
public:
	void Call(const Scaleform::GFx::FunctionHandler::Params&) override
	{
		std::string host;
		std::uint16_t port = cmp::kDefaultPort;
		std::string err;
		if (!ParseJoinAddress(host, port, err)) {
			SetStatus(err);
			return;
		}
		CMP_SaveNetworkSettings(host, port);
		CloseJoinPanel();
		SetFlow(Flow::Querying);
		SetStatus("querying " + host + ":" + std::to_string(port));
		CMP_QueryStart(host, port);
	}
};

class JoinCancelFn : public Scaleform::GFx::FunctionHandler
{
public:
	void Call(const Scaleform::GFx::FunctionHandler::Params&) override
	{
		CloseJoinPanel();
		SetFlow(Flow::Idle);
		SetStatus("join cancelled");
	}
};

class OpenJoinFn : public Scaleform::GFx::FunctionHandler
{
public:
	void Call(const Scaleform::GFx::FunctionHandler::Params&) override
	{
		OpenJoinPanel();
	}
};

class JoinListPressFn : public Scaleform::GFx::FunctionHandler
{
public:
	void Call(const Scaleform::GFx::FunctionHandler::Params&) override
	{
		if (g_joinListIndex < 0 || !g_joinList.IsObject()) {
			return;
		}
		GFxValue selected;
		if (!g_joinList.GetMember("selectedIndex", &selected)) {
			return;
		}
		const auto idx = static_cast<std::int32_t>(selected.GetNumber());
		if (idx == g_joinListIndex) {
			OpenJoinPanel();
		}
	}
};

void OpenJoinPanel()
{
	if (g_joinPanelOpen || g_flow != Flow::Idle) {
		return;
	}
	auto* mm = MainMenuPtr();
	if (!mm || !mm->uiMovie) {
		SetStatus("no MainMenu for join panel");
		return;
	}
	GFxMovie* movie = mm->uiMovie.get();
	GFxValue root;
	if (!movie->GetVariable(&root, "_root") || !root.IsObject()) {
		return;
	}

	if (!g_joinConfirmFn) {
		g_joinConfirmFn = new JoinConfirmFn();
		g_joinCancelFn = new JoinCancelFn();
		g_openJoinFn = new OpenJoinFn();
		g_joinListPressFn = new JoinListPressFn();
	}

	movie->CreateObject(&g_joinPanel, "flash.display.Sprite");
	if (!(g_joinPanel.IsObject() || g_joinPanel.IsDisplayObject())) {
		// Fallback: MessageMenu without address edit.
		auto& s = CMP_Session();
		g_statusStorage = "Connect to " + s.settings.host + ":" + std::to_string(s.settings.port)
			+ "\n(Could not open edit panel; change Host/Port in CommonwealthMP.ini)";
		auto* mgr = RE::MessageMenuManager::GetSingleton();
		if (!mgr) {
			return;
		}
		auto* cb = new JoinBoxCallback();
		mgr->Create("JOIN SERVER", g_statusStorage.c_str(), cb, RE::WARNING_TYPES::kMenus, "JOIN", "BACK", nullptr, nullptr, true);
		g_joinPrompted = true;
		return;
	}

	auto& s = CMP_Session();
	GFxValue bg;
	movie->CreateObject(&bg, "flash.text.TextField");
	bg.SetMember("x", GFxValue(0.0));
	bg.SetMember("y", GFxValue(0.0));
	bg.SetMember("width", GFxValue(560.0));
	bg.SetMember("height", GFxValue(260.0));
	bg.SetMember("background", GFxValue(true));
	bg.SetMember("border", GFxValue(true));
	bg.SetMember("backgroundColor", GFxValue(static_cast<double>(0x101010)));
	bg.SetMember("borderColor", GFxValue(static_cast<double>(0xC4A574)));
	bg.SetMember("selectable", GFxValue(false));
	bg.SetMember("mouseEnabled", GFxValue(false));
	g_joinPanel.Invoke("addChild", nullptr, &bg, 1);

	MakeLabel(movie, g_joinPanel, "JOIN SERVER", 20.0, 16.0);
	MakeLabel(movie, g_joinPanel, "Host / IP", 20.0, 56.0);
	MakeInput(movie, g_joinPanel, g_hostField, s.settings.host.c_str(), 20.0, 82.0, 520.0);
	MakeLabel(movie, g_joinPanel, "Port", 20.0, 122.0);
	MakeInput(movie, g_joinPanel, g_portField, std::to_string(s.settings.port).c_str(), 20.0, 148.0, 160.0);
	MakeLabel(movie, g_joinPanel, "Menu join needs a live Commonwealth exterior host.", 20.0, 188.0, 520.0);
	MakeButton(movie, g_joinPanel, "JOIN", 20.0, 214.0, g_joinConfirmFn);
	MakeButton(movie, g_joinPanel, "BACK", 200.0, 214.0, g_joinCancelFn);

	g_joinPanel.SetMember("x", GFxValue(360.0));
	g_joinPanel.SetMember("y", GFxValue(220.0));
	root.Invoke("addChild", nullptr, &g_joinPanel, 1);

	GFxValue stage;
	if (movie->GetVariable(&stage, "_root.stage") || movie->GetVariable(&stage, "stage")) {
		if (stage.IsObject()) {
			stage.SetMember("focus", g_hostField);
		}
	}
	g_hostField.Invoke("setSelection", nullptr, nullptr, 0);

	g_joinPanelOpen = true;
	g_joinPrompted = true;
	REX::INFO("Join panel open Host={}:{}", s.settings.host, s.settings.port);
}

void OpenJoinBox()
{
	OpenJoinPanel();
}

void AttachJoinOverlay(RE::IMenu* menu)
{
	if (g_overlayAttached || !menu || !menu->uiMovie) {
		return;
	}
	if (!g_openJoinFn) {
		g_joinConfirmFn = new JoinConfirmFn();
		g_joinCancelFn = new JoinCancelFn();
		g_openJoinFn = new OpenJoinFn();
		g_joinListPressFn = new JoinListPressFn();
	}
	GFxValue root;
	if (!menu->uiMovie->GetVariable(&root, "_root") || !root.IsObject()) {
		return;
	}
	GFxValue fn;
	menu->uiMovie->CreateFunction(&fn, g_openJoinFn);
	GFxValue tf;
	menu->uiMovie->CreateObject(&tf, "flash.text.TextField");
	if (!(tf.IsObject() || tf.IsDisplayObject())) {
		return;
	}
	tf.SetMember("text", GFxValue(g_joinLabel.c_str()));
	tf.SetMember("x", GFxValue(36.0));
	tf.SetMember("y", GFxValue(640.0));
	tf.SetMember("width", GFxValue(280.0));
	tf.SetMember("height", GFxValue(28.0));
	tf.SetMember("selectable", GFxValue(false));
	const std::string html = std::string("<font color='#F5F0E6' size='18'><b>") + g_joinLabel + "</b></font>";
	tf.SetMember("htmlText", GFxValue(html.c_str()));
	root.Invoke("addChild", nullptr, &tf, 1);
	GFxValue click("click");
	GFxValue args[2]{ click, fn };
	tf.Invoke("addEventListener", nullptr, args, 2);
	tf.SetMember("onPress", fn);
	g_overlayAttached = true;
	REX::INFO("MainMenu JOIN SERVER overlay attached");
}

void WireJoinListPress(RE::IMenu* menu)
{
	if (!menu || !menu->uiMovie || !g_joinList.IsObject() || g_joinListIndex < 0) {
		return;
	}
	if (!g_joinListPressFn) {
		g_joinConfirmFn = new JoinConfirmFn();
		g_joinCancelFn = new JoinCancelFn();
		g_openJoinFn = new OpenJoinFn();
		g_joinListPressFn = new JoinListPressFn();
	}
	GFxValue fn;
	menu->uiMovie->CreateFunction(&fn, g_joinListPressFn);
	static const char* kEvents[] = { "itemPress", "ListItemsPress", "itemClick", "click" };
	for (const char* ev : kEvents) {
		GFxValue type(ev);
		GFxValue args[2]{ type, fn };
		g_joinList.Invoke("addEventListener", nullptr, args, 2);
	}
	g_joinList.SetMember("onItemPress", fn);
}

void OnMainMenuOpen(RE::IMenu* menu)
{
	StampVersion(menu);
	TryInjectJoinRow(menu);
	WireJoinListPress(menu);
	AttachJoinOverlay(menu);
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
			s.menuJoin = false;
		}
		break;
	case Flow::WaitWorld: {
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && player->GetParentCell()) {
			SetFlow(Flow::SkipIntro);
		} else if (TimedOut(90.0)) {
			SetFlow(Flow::Failed);
			SetStatus("timed out waiting for world");
			s.menuJoin = false;
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
			s.menuJoin = false;
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
				s.menuJoin = false;
			}
			break;
		}
		if (CMP_Join(s.settings.host, s.settings.port, cmp::kHelloFlagRequireHost)) {
			s.menuJoin = false;
			SetFlow(Flow::Done);
			SetStatus("joined host");
		} else {
			SetFlow(Flow::Failed);
			s.menuJoin = false;
		}
		break;
	default:
		break;
	}
}

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
				g_stamped = false;
				g_joinInjected = false;
				g_overlayAttached = false;
				g_joinPrompted = false;
				g_joinListIndex = -1;
				g_joinList = nullptr;
				CloseJoinPanel();
				if (auto* ui = RE::UI::GetSingleton()) {
					if (const auto ptr = ui->GetMenu<RE::MainMenu>()) {
						OnMainMenuOpen(ptr.get());
					}
				}
			}
		} else if (std::strcmp(name, "LooksMenu") == 0) {
			g_looksOpen = a_event.opening;
		} else if (std::strcmp(name, "SPECIALMenu") == 0) {
			g_specialOpen = a_event.opening;
		}
		return RE::BSEventNotifyControl::kContinue;
	}
};

MenuSink g_sink;

}  // namespace

std::string CMP_VersionStamp()
{
	return std::string("CMP ") + F4SE::GetPluginVersion().string() + "  " + CMP_BUILD_STAMP;
}

bool CMP_MenuJoinPending()
{
	return g_flow != Flow::Idle && g_flow != Flow::Done && g_flow != Flow::Failed;
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
	if (!g_sinkReady) {
		CMP_InstallMenu();
	}
	if (auto* ui = RE::UI::GetSingleton(); ui && ui->GetMenuOpen("MainMenu"sv)) {
		if (const auto ptr = ui->GetMenu<RE::MainMenu>()) {
			if (!g_stamped) {
				OnMainMenuOpen(ptr.get());
			}
			// Activate on the injected JOIN SERVER row opens the edit panel.
			static bool wasListPress = false;
			const bool listPress = ptr->debounceMainListPress;
			if (listPress && !wasListPress && !g_joinPanelOpen && g_flow == Flow::Idle
				&& g_joinListIndex >= 0 && g_joinList.IsObject()) {
				GFxValue selected;
				if (g_joinList.GetMember("selectedIndex", &selected)
					&& static_cast<std::int32_t>(selected.GetNumber()) == g_joinListIndex) {
					OpenJoinPanel();
				}
			}
			wasListPress = listPress;
		}
	}
	TickFlow();
}

void CMP_MenuOnNewGame()
{
	if (g_flow == Flow::StartingGame || CMP_Session().menuJoin) {
		SetFlow(Flow::WaitWorld);
		SetStatus("new game loaded, waiting for player");
	}
}
