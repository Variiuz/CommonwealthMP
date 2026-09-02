#include "pch.h"
#include "render_hook.h"
#include "overlay.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#ifdef ERROR
#undef ERROR
#endif

namespace {

using Present_t = HRESULT(__stdcall*)(REX::W32::IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags);
using ResizeBuffers_t = HRESULT(__stdcall*)(
	REX::W32::IDXGISwapChain* a_swapChain,
	UINT a_bufferCount,
	UINT a_width,
	UINT a_height,
	UINT a_newFormat,
	UINT a_swapChainFlags);

Present_t g_origPresent = nullptr;
ResizeBuffers_t g_origResizeBuffers = nullptr;

REX::W32::IDXGISwapChain* g_swapChain = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
HWND g_hwnd = nullptr;
ID3D11RenderTargetView* g_mainRtv = nullptr;

bool g_hooked = false;
bool g_initialized = false;

void CleanupRenderTarget();

void CreateRenderTarget()
{
	if (!g_swapChain || !g_device) {
		return;
	}
	CleanupRenderTarget();
	auto* rawSwapChain = reinterpret_cast<::IDXGISwapChain*>(g_swapChain);
	ID3D11Texture2D* backBuffer = nullptr;
	if (SUCCEEDED(rawSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
		g_device->CreateRenderTargetView(backBuffer, nullptr, &g_mainRtv);
		backBuffer->Release();
	}
}

void CleanupRenderTarget()
{
	if (g_mainRtv) {
		g_mainRtv->Release();
		g_mainRtv = nullptr;
	}
}

HRESULT __stdcall ResizeBuffersHook(
	REX::W32::IDXGISwapChain* a_swapChain,
	UINT a_bufferCount,
	UINT a_width,
	UINT a_height,
	UINT a_newFormat,
	UINT a_swapChainFlags)
{
	CleanupRenderTarget();
	if (g_origResizeBuffers) {
		return g_origResizeBuffers(a_swapChain, a_bufferCount, a_width, a_height, a_newFormat, a_swapChainFlags);
	}
	return E_FAIL;
}

HRESULT __stdcall PresentHook(REX::W32::IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
{
	if (!g_initialized) {
		auto* rd = RE::BSGraphics::GetRendererData();
		if (rd && rd->device && rd->context && rd->renderWindow[0].swapChain && rd->renderWindow[0].hwnd) {
			g_device = reinterpret_cast<ID3D11Device*>(rd->device);
			g_context = reinterpret_cast<ID3D11DeviceContext*>(rd->context);
			g_swapChain = rd->renderWindow[0].swapChain;
			g_hwnd = reinterpret_cast<HWND>(rd->renderWindow[0].hwnd);
			CMP_Overlay_Init(g_device, g_context, g_hwnd);
			CreateRenderTarget();
			g_initialized = true;
			REX::INFO("CMP overlay initialized (D3D11 Present hook)");
		}
	}

	if (g_initialized) {
		if (!g_mainRtv) {
			CreateRenderTarget();
		}
		CMP_Overlay_Draw(g_mainRtv);
	}

	return g_origPresent(a_swapChain, a_syncInterval, a_flags);
}

} // namespace

void CMP_RenderHook_Install()
{
	if (g_hooked) {
		return;
	}
	auto* rd = RE::BSGraphics::GetRendererData();
	if (!rd || !rd->initialized || !rd->renderWindow[0].swapChain) {
		return;
	}
	auto* sc = rd->renderWindow[0].swapChain;
	auto* vtable = *reinterpret_cast<std::uintptr_t**>(sc);

	DWORD oldProtect = 0;
	if (!VirtualProtect(vtable + 8, sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		REX::ERROR("CMP_RenderHook_Install: VirtualProtect failed for Present");
		return;
	}
	g_origPresent = reinterpret_cast<Present_t>(vtable[8]);
	vtable[8] = reinterpret_cast<std::uintptr_t>(&PresentHook);
	VirtualProtect(vtable + 8, sizeof(std::uintptr_t), oldProtect, &oldProtect);

	if (VirtualProtect(vtable + 13, sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		g_origResizeBuffers = reinterpret_cast<ResizeBuffers_t>(vtable[13]);
		vtable[13] = reinterpret_cast<std::uintptr_t>(&ResizeBuffersHook);
		VirtualProtect(vtable + 13, sizeof(std::uintptr_t), oldProtect, &oldProtect);
	}

	g_swapChain = sc;
	g_device = reinterpret_cast<ID3D11Device*>(rd->device);
	g_context = reinterpret_cast<ID3D11DeviceContext*>(rd->context);
	g_hwnd = reinterpret_cast<HWND>(rd->renderWindow[0].hwnd);
	g_hooked = true;
	REX::INFO("CMP_RenderHook_Install: Present hook installed");
}

void CMP_RenderHook_Shutdown()
{
	if (!g_hooked || !g_swapChain) {
		return;
	}
	CMP_Overlay_Shutdown();
	CleanupRenderTarget();

	auto* vtable = *reinterpret_cast<std::uintptr_t**>(g_swapChain);
	DWORD oldProtect = 0;
	if (VirtualProtect(vtable + 8, sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		vtable[8] = reinterpret_cast<std::uintptr_t>(g_origPresent);
		VirtualProtect(vtable + 8, sizeof(std::uintptr_t), oldProtect, &oldProtect);
	}
	if (g_origResizeBuffers && VirtualProtect(vtable + 13, sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		vtable[13] = reinterpret_cast<std::uintptr_t>(g_origResizeBuffers);
		VirtualProtect(vtable + 13, sizeof(std::uintptr_t), oldProtect, &oldProtect);
	}
	g_hooked = false;
}
