#include "pch.h"
#include "session.h"
#include "overlay.h"
#include "menu.h"
#include "net.h"
#include "ghost.h"
#include "dump.h"
#include "pointer.h"
#include "input_hook.h"
#include "render_hook.h"
#include "render_hook.h"
#include "input_hook.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#ifdef ERROR
#undef ERROR
#endif

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#include <atomic>
#include <mutex>

#include "RE/C/ControlMap.h"

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
HWND g_hwnd = nullptr;
bool g_initialized = false;
std::atomic<bool> g_visible{ false };
std::atomic<int> g_pendingAction{ 0 };

constexpr int kActionNone = 0;
constexpr int kActionDump = 1;
constexpr int kActionLeave = 2;

void UpdateCachedState()
{
	auto& s = CMP_Session();
	auto status = CMP_StatusText();
	auto pointer = CMP_PointerText();

	std::vector<std::pair<std::uint32_t, std::string>> peers;
	{
		std::lock_guard lock(s.mutex);
		for (const auto& [peer, pose] : s.net.latestPose) {
			if (peer == s.net.myPeerId) {
				continue;
			}
			std::string name;
			if (auto it = s.ghosts.names.find(peer); it != s.ghosts.names.end()) {
				name = it->second;
			}
			peers.emplace_back(peer, name);
		}
	}

	{
		std::lock_guard lock(s.overlay.mutex);
		s.overlay.status = std::move(status);
		s.overlay.pointer = std::move(pointer);
		s.overlay.peers = std::move(peers);
	}
}

void DrawChatPanel()
{
	auto& s = CMP_Session();
	if (!s.settings.overlayChatOpen) {
		return;
	}

	std::vector<std::string> history;
	{
		std::lock_guard lock(s.overlay.chatMutex);
		history = s.overlay.chatHistory;
	}

	static char inputBuf[256] = "";
	static bool scrollToBottom = false;

	if (ImGui::Begin("CMP Chat", &s.settings.overlayChatOpen)) {
		ImGui::BeginChild("History", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
		for (const auto& line : history) {
			ImGui::TextUnformatted(line.c_str());
		}
		if (scrollToBottom) {
			ImGui::SetScrollHereY(1.0f);
			scrollToBottom = false;
		}
		ImGui::EndChild();

		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x - 60.0f);
		if (ImGui::InputText("##chatInput", inputBuf, sizeof(inputBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
			if (inputBuf[0]) {
				CMP_SendChat(inputBuf);
				inputBuf[0] = 0;
				scrollToBottom = true;
			}
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Send", ImVec2(60.0f, 0.0f)) && inputBuf[0]) {
			CMP_SendChat(inputBuf);
			inputBuf[0] = 0;
			scrollToBottom = true;
		}
	}
	ImGui::End();
}

void DrawDebugPanel()
{
	auto& s = CMP_Session();
	if (!s.settings.overlayDebugOpen) {
		return;
	}

	std::string status;
	std::string pointer;
	std::vector<std::pair<std::uint32_t, std::string>> peers;
	{
		std::lock_guard lock(s.overlay.mutex);
		status = s.overlay.status;
		pointer = s.overlay.pointer;
		peers = s.overlay.peers;
	}

	if (ImGui::Begin("CMP Debug", &s.settings.overlayDebugOpen)) {
		ImGui::TextWrapped("Status: %s", status.c_str());
		ImGui::TextWrapped("Pointer: %s", pointer.c_str());
		ImGui::Separator();

		ImGui::Text("Peers (%zu):", peers.size());
		for (const auto& [peer, name] : peers) {
			ImGui::BulletText("[%u] %s", peer, name.empty() ? "?" : name.c_str());
		}

		ImGui::Separator();
		if (ImGui::Button("Dump")) {
			g_pendingAction.store(kActionDump);
		}
		ImGui::SameLine();
		if (ImGui::Button("Leave")) {
			g_pendingAction.store(kActionLeave);
		}
	}
	ImGui::End();
}

} // namespace

void CMP_Overlay_Init(void* device, void* context, void* hwnd)
{
	if (g_initialized) {
		return;
	}
	g_device = reinterpret_cast<ID3D11Device*>(device);
	g_context = reinterpret_cast<ID3D11DeviceContext*>(context);
	g_hwnd = reinterpret_cast<HWND>(hwnd);
	if (!g_device || !g_context || !g_hwnd) {
		REX::ERROR("CMP_Overlay_Init: invalid device/context/hwnd");
		return;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(g_hwnd);
	ImGui_ImplDX11_Init(g_device, g_context);

	g_visible.store(CMP_Session().settings.overlayVisible);
	g_initialized = true;
}

void CMP_Overlay_Shutdown()
{
	if (!g_initialized) {
		return;
	}
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	g_device = nullptr;
	g_context = nullptr;
	g_hwnd = nullptr;
	g_initialized = false;
}

void UpdateOverlayInputCapture()
{
	const bool capture = CMP_Overlay_IsVisible() || CMP_MenuFormOpen();
	if (auto* controlMap = RE::ControlMap::GetSingleton()) {
		controlMap->SetIgnoreKeyboardMouse(capture);
	}
}

void UpdateOverlayMouseState()
{
	if (!g_hwnd || !g_initialized) {
		return;
	}
	if (!g_visible.load() && !CMP_MenuFormOpen()) {
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	while (::ShowCursor(TRUE) < 0) {
	}
	io.MouseDrawCursor = true;

	POINT pos{};
	if (::GetCursorPos(&pos) && ::ScreenToClient(g_hwnd, &pos)) {
		io.AddMousePosEvent(static_cast<float>(pos.x), static_cast<float>(pos.y));
	}

	static const int kMouseButtons[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
	for (int i = 0; i < IM_ARRAYSIZE(kMouseButtons); ++i) {
		const bool down = (::GetAsyncKeyState(kMouseButtons[i]) & 0x8000) != 0;
		io.AddMouseButtonEvent(i, down);
	}
}

void CMP_Overlay_Tick()
{
	CMP_RenderHook_Install();
	CMP_InputHook_Install();
	UpdateOverlayInputCapture();
	UpdateCachedState();

	int action = g_pendingAction.exchange(kActionNone);
	if (action == kActionDump) {
		CMP_DumpLive();
	} else if (action == kActionLeave) {
		CMP_Leave();
		CMP_DespawnGhosts();
	}
}

void CMP_Overlay_SetVisible(bool visible)
{
	g_visible.store(visible);
	CMP_Session().settings.overlayVisible = visible;
}

bool CMP_Overlay_IsVisible()
{
	return g_visible.load();
}

void CMP_Overlay_Toggle()
{
	CMP_Overlay_SetVisible(!CMP_Overlay_IsVisible());
}

void CMP_Overlay_Draw(void* renderTargetView)
{
	if (!g_initialized || !g_context || !renderTargetView) {
		return;
	}

	auto& s = CMP_Session();
	const bool showPanels = g_visible.load();
	const bool showMenuForm = CMP_MenuFormOpen();
	const bool showNameplates = s.net.joined;
	if (!showPanels && !showNameplates && !showMenuForm) {
		return;
	}

	ImGui::GetIO().FontGlobalScale = s.settings.overlayFontScale;

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	UpdateOverlayMouseState();
	ImGui::NewFrame();

	RECT rect = {};
	float viewportW = 1920.0f;
	float viewportH = 1080.0f;
	if (g_hwnd && GetClientRect(g_hwnd, &rect)) {
		viewportW = static_cast<float>(rect.right - rect.left);
		viewportH = static_cast<float>(rect.bottom - rect.top);
	}

	if (showNameplates) {
		CMP_DrawGhostNameplates(ImGui::GetForegroundDrawList(), viewportW, viewportH);
	}

	if (showMenuForm) {
		CMP_MenuDrawForms();
	}

	if (showPanels) {
		DrawChatPanel();
		DrawDebugPanel();
	}

	ImGui::Render();

	if (g_hwnd && GetClientRect(g_hwnd, &rect)) {
		D3D11_VIEWPORT vp = {};
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		vp.Width = static_cast<float>(rect.right - rect.left);
		vp.Height = static_cast<float>(rect.bottom - rect.top);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_context->RSSetViewports(1, &vp);
	}

	ID3D11RenderTargetView* rtv = reinterpret_cast<ID3D11RenderTargetView*>(renderTargetView);
	g_context->OMSetRenderTargets(1, &rtv, nullptr);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
