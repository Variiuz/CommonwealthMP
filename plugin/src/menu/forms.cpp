#include "pch.h"
#include "menu/internal.h"
#include "menu.h"

#include "config.h"
#include "host_launch.h"
#include "net.h"
#include "session.h"

#include "RE/B/BGSSaveLoadManager.h"
#include "RE/B/BGSSaveLoadFileEntry.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace cmp_menu {

void CopyJoinField(char* dest, std::size_t destSize, const char* src)
{
	if (!dest || destSize == 0) {
		return;
	}
	std::snprintf(dest, destSize, "%s", src ? src : "");
}

void CloseMenuForm()
{
	g_menuForm = MenuForm::None;
	g_menuPopupQueued = false;
	g_hostSavesRefreshed = false;
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

void SyncJoinFieldsFromSettings()
{
	auto& s = CMP_Session();
	const std::string addr = s.settings.host + ":" + std::to_string(s.settings.port);
	std::snprintf(g_joinHost, sizeof(g_joinHost), "%s", addr.c_str());
	std::snprintf(g_joinPort, sizeof(g_joinPort), "%s", std::to_string(s.settings.port).c_str());
	std::snprintf(g_joinName, sizeof(g_joinName), "%s", s.settings.playerName.c_str());
	std::snprintf(g_joinPassword, sizeof(g_joinPassword), "%s", s.settings.password.c_str());
}

bool ParseJoinAddress(std::string& hostOut, std::uint16_t& portOut, std::string& err)
{
	auto host = TrimCopy(g_joinHost);
	auto portStr = TrimCopy(g_joinPort);
	const auto colon = host.find(':');
	if (colon != std::string::npos) {
		portStr = TrimCopy(host.substr(colon + 1));
		host = TrimCopy(host.substr(0, colon));
	}
	if (host.empty()) {
		err = "server address is empty";
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

bool ParsePortField(std::uint16_t& portOut, std::string& err)
{
	const auto portStr = TrimCopy(g_joinPort);
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
	portOut = static_cast<std::uint16_t>(port);
	return true;
}

void RefreshHostSaveList()
{
	g_hostSaves.clear();
	g_hostSaveSelection = -1;

	auto* mgr = RE::BGSSaveLoadManager::GetSingleton();
	if (!mgr) {
		return;
	}

	const std::uint64_t playerId = mgr->displayPlayerID ? mgr->displayPlayerID : mgr->currentPlayerID;
	mgr->BuildSaveGameList(playerId);

	for (std::uint32_t i = 0; i < mgr->saveGameList.size(); ++i) {
		auto* entry = mgr->saveGameList[i];
		if (!entry || entry->corrupt) {
			continue;
		}

		HostSaveRow row;
		row.listIndex = static_cast<int>(i);

		std::string label;
		if (entry->playerName && entry->playerName[0]) {
			label = entry->playerName;
		} else {
			label = "Save " + std::to_string(entry->saveGameNumber);
		}
		if (entry->location && entry->location[0]) {
			label += " | ";
			label += entry->location;
		}
		if (entry->playTime && entry->playTime[0]) {
			label += " | ";
			label += entry->playTime;
		}
		row.label = std::move(label);
		g_hostSaves.push_back(std::move(row));
	}

	if (!g_hostSaves.empty()) {
		g_hostSaveSelection = 0;
	}
}

void LoadSaveFromTitleMenu(int saveIndex)
{
	auto* mm = MainMenuPtr();
	if (!mm) {
		SetStatus("main menu missing");
		return;
	}
	mm->DoLoadGame(saveIndex);
}

void ConfirmHost()
{
	if (CMP_Session().net.joined) {
		SetStatus("already in session");
		return;
	}

	std::uint16_t port = cmp::kDefaultPort;
	std::string err;
	if (!ParsePortField(port, err)) {
		SetStatus(err);
		return;
	}

	CMP_SaveNetworkSettings("127.0.0.1", port);
	CMP_SavePassword(TrimCopy(g_joinPassword));

	if (!CMP_LaunchServer(port, err)) {
		SetStatus(err);
		return;
	}

	if (g_hostContext == JoinContext::TitleMenu) {
		if (g_hostSaveSelection < 0 || g_hostSaveSelection >= static_cast<int>(g_hostSaves.size())) {
			SetStatus("select a save");
			return;
		}
		g_pendingMenuHostJoin = true;
		const int loadIndex = g_hostSaves[static_cast<std::size_t>(g_hostSaveSelection)].listIndex;
		CloseMenuForm();
		LoadSaveFromTitleMenu(loadIndex);
		SetStatus("loading save for host...");
		return;
	}

	CloseMenuForm();
	if (CMP_Join("127.0.0.1", port)) {
		SetStatus("connected as host");
		ClosePauseMenu();
	} else {
		SetStatus("host join failed");
	}
}

void ConfirmJoin()
{
	std::string host;
	std::uint16_t port = cmp::kDefaultPort;
	std::string err;
	if (!ParseJoinAddress(host, port, err)) {
		SetStatus(err);
		return;
	}
	CMP_SaveNetworkSettings(host, port);
	CMP_SavePlayerName(TrimCopy(g_joinName));
	CMP_SavePassword(TrimCopy(g_joinPassword));
	auto& s = CMP_Session();
	if (s.settings.password.size() > 15) {
		s.settings.password.resize(15);
	}
	CloseMenuForm();
	SetFlow(Flow::Querying);
	SetStatus("querying " + host + ":" + std::to_string(port));
	CMP_QueryStart(host, port);
}

void OpenHostFlow(JoinContext ctx)
{
	if (ctx == JoinContext::InGame && CMP_Session().net.joined) {
		SetStatus("already in session");
		return;
	}
	if (g_flow != Flow::Idle) {
		return;
	}
	g_hostContext = ctx;
	SyncJoinFieldsFromSettings();
	g_hostSavesRefreshed = false;
	g_hostSaveSelection = -1;
	g_menuForm = MenuForm::Host;
	g_menuPopupQueued = true;
	REX::INFO("Host panel opened ({} menu)", ctx == JoinContext::TitleMenu ? "title" : "pause");
}

void DisconnectSession()
{
	CMP_Leave();
	SetStatus("disconnected");
	ClosePauseMenu();
}

void OpenJoinFlow(JoinContext ctx)
{
	g_joinContext = ctx;
	if (g_flow != Flow::Idle) {
		return;
	}
	SyncJoinFieldsFromSettings();
	g_menuForm = MenuForm::Join;
	g_menuPopupQueued = true;
	REX::INFO("Join panel opened ({} menu)", ctx == JoinContext::TitleMenu ? "title" : "pause");
}

}  // namespace cmp_menu

void CMP_MenuDrawForms()
{
	using namespace cmp_menu;

	if (g_menuForm == MenuForm::None) {
		return;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 vpPos = viewport->Pos;
	const ImVec2 vpSize = viewport->Size;
	ImDrawList* bg = ImGui::GetBackgroundDrawList();
	bg->AddRectFilled(vpPos, ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y), IM_COL32(0, 0, 0, 170));

	const char* popupId = g_menuForm == MenuForm::Join ? "JOIN" : "HOST";
	if (g_menuPopupQueued) {
		ImGui::OpenPopup(popupId);
		g_menuPopupQueued = false;
	}

	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.07f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));

	const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
	if (!ImGui::BeginPopupModal(popupId, nullptr, flags)) {
		ImGui::PopStyleColor(6);
		ImGui::PopStyleVar(2);
		return;
	}

	if (g_menuForm == MenuForm::Join) {
		ImGui::TextUnformatted("Server");
		ImGui::SetNextItemWidth(440.0f);
		ImGui::InputText("##server", g_joinHost, sizeof(g_joinHost));

		ImGui::TextUnformatted("Name");
		ImGui::SetNextItemWidth(440.0f);
		ImGui::InputText("##name", g_joinName, sizeof(g_joinName));

		ImGui::TextUnformatted("Password");
		ImGui::SetNextItemWidth(440.0f);
		ImGui::InputText("##password", g_joinPassword, sizeof(g_joinPassword), ImGuiInputTextFlags_Password);

		ImGui::Spacing();
		if (ImGui::Button("Join", ImVec2(140.0f, 0.0f))) {
			ConfirmJoin();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f))) {
			CloseMenuForm();
			ImGui::CloseCurrentPopup();
		}
	} else {
		if (g_hostContext == JoinContext::TitleMenu) {
			if (!g_hostSavesRefreshed) {
				RefreshHostSaveList();
				g_hostSavesRefreshed = true;
			}
			ImGui::TextUnformatted("Save");
			const ImVec2 listSize(440.0f, 220.0f);
			if (ImGui::BeginChild("##host_saves", listSize, true)) {
				if (g_hostSaves.empty()) {
					ImGui::TextUnformatted("No saves found");
				} else {
					for (int i = 0; i < static_cast<int>(g_hostSaves.size()); ++i) {
						const bool selected = g_hostSaveSelection == i;
						if (ImGui::Selectable(g_hostSaves[static_cast<std::size_t>(i)].label.c_str(), selected)) {
							g_hostSaveSelection = i;
						}
					}
				}
			}
			ImGui::EndChild();
		} else {
			ImGui::TextUnformatted("Current save (in world)");
		}

		ImGui::TextUnformatted("Port");
		ImGui::SetNextItemWidth(440.0f);
		ImGui::InputText("##host_port", g_joinPort, sizeof(g_joinPort));

		ImGui::TextUnformatted("Password");
		ImGui::SetNextItemWidth(440.0f);
		ImGui::InputText("##host_password", g_joinPassword, sizeof(g_joinPassword), ImGuiInputTextFlags_Password);

		ImGui::Spacing();
		if (ImGui::Button("Host", ImVec2(140.0f, 0.0f))) {
			ConfirmHost();
			if (g_menuForm == MenuForm::None) {
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f))) {
			CloseMenuForm();
			ImGui::CloseCurrentPopup();
		}
	}

	ImGui::EndPopup();
	ImGui::PopStyleColor(6);
	ImGui::PopStyleVar(2);
}
