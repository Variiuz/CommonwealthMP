#include "pch.h"
#include "input_hook.h"
#include "session.h"
#include "overlay.h"
#include "menu.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <REX/FModule.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

WNDPROC g_prevWndProc = nullptr;
HWND g_hookedHwnd = nullptr;
bool g_installed = false;

using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
ClipCursor_t g_origClipCursor = nullptr;
bool g_clipCursorHooked = false;

bool IsMouseMessage(UINT msg)
{
	return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL;
}

bool IsKeyboardMessage(UINT msg)
{
	switch (msg) {
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_CHAR:
	case WM_SYSCHAR:
	case WM_UNICHAR:
	case WM_IME_CHAR:
	case WM_IME_COMPOSITION:
		return true;
	default:
		return false;
	}
}

BOOL WINAPI ClipCursorHook(const RECT* rect)
{
	if (CMP_Overlay_IsVisible() || CMP_MenuFormOpen()) {
		RECT clientRect{};
		if (::GetClientRect(g_hookedHwnd, &clientRect)) {
			POINT topLeft{ clientRect.left, clientRect.top };
			POINT bottomRight{ clientRect.right, clientRect.bottom };
			::ClientToScreen(g_hookedHwnd, &topLeft);
			::ClientToScreen(g_hookedHwnd, &bottomRight);
			RECT screenRect{
				topLeft.x,
				topLeft.y,
				bottomRight.x,
				bottomRight.y
			};
			return g_origClipCursor(&screenRect);
		}
		return g_origClipCursor(nullptr);
	}

	return g_origClipCursor(rect);
}

void InstallClipCursorHook()
{
	if (g_clipCursorHooked) {
		return;
	}

	constexpr std::string_view kUser32{ "user32.dll" };
	constexpr std::string_view kClipCursor{ "ClipCursor" };
	const auto mod = REX::FModule::GetExecutingModule();
	auto* iatSlot = mod.GetImportFunctionPointer(kClipCursor, kUser32);
	if (!iatSlot) {
		REX::WARN("CMP_InputHook_Install: ClipCursor IAT hook failed");
		return;
	}

	g_origClipCursor = *reinterpret_cast<ClipCursor_t*>(iatSlot);
	mod.SetImportFunctionPointer(kClipCursor, kUser32, reinterpret_cast<void*>(&ClipCursorHook));
	g_clipCursorHooked = true;
	REX::INFO("CMP_InputHook_Install: ClipCursor hooked");
}

LRESULT CALLBACK InputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && wParam == CMP_Session().settings.overlayToggleKey) {
		CMP_Overlay_Toggle();
		return 0;
	}

	if (CMP_Overlay_IsVisible() || CMP_MenuFormOpen()) {
		const LRESULT imguiResult = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
		if (imguiResult != 0) {
			return imguiResult;
		}

		if (IsMouseMessage(msg) || IsKeyboardMessage(msg)) {
			return 0;
		}
	}

	if (g_prevWndProc) {
		return CallWindowProcW(g_prevWndProc, hWnd, msg, wParam, lParam);
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

void CMP_InputHook_Install()
{
	if (!g_clipCursorHooked) {
		InstallClipCursorHook();
	}

	if (g_installed) {
		return;
	}
	auto* main = RE::Main::GetSingleton();
	if (!main || !main->hwnd) {
		return;
	}
	HWND hwnd = reinterpret_cast<HWND>(main->hwnd);
	g_prevWndProc = reinterpret_cast<WNDPROC>(
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(InputWndProc)));
	if (g_prevWndProc) {
		g_hookedHwnd = hwnd;
		g_installed = true;
		REX::INFO("CMP_InputHook_Install: WndProc hooked");
	}
}

void CMP_InputHook_Shutdown()
{
	if (!g_installed || !g_hookedHwnd || !g_prevWndProc) {
		return;
	}
	SetWindowLongPtrW(g_hookedHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prevWndProc));
	g_installed = false;
	g_hookedHwnd = nullptr;
	g_prevWndProc = nullptr;
}
