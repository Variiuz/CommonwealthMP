#include "pch.h"
#include "scaleform_menu.h"
#include "menu.h"

#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_ASMovieRootBase.h"
#include "Scaleform/G/GFx_Value.h"

#include <cstring>
#include <string_view>

namespace {

using Movie = Scaleform::GFx::Movie;
using Value = Scaleform::GFx::Value;
using FunctionHandler = Scaleform::GFx::FunctionHandler;

constexpr auto kHostSwf = "Interface/MainMenu.swf"sv;
constexpr auto kCompanionSwf = "CommonwealthMP_Menu.swf";

class OpenJoinFn : public FunctionHandler
{
public:
	void Call(const Params& a_params) override
	{
		CMP_MenuOpenJoin(false);
		if (a_params.retVal) {
			*a_params.retVal = true;
		}
	}
};

class OpenJoinTitleFn : public FunctionHandler
{
public:
	void Call(const Params& a_params) override
	{
		CMP_MenuOpenJoin(true);
		if (a_params.retVal) {
			*a_params.retVal = true;
		}
	}
};

class OpenHostFn : public FunctionHandler
{
public:
	void Call(const Params& a_params) override
	{
		CMP_MenuOpenHost(false);
		if (a_params.retVal) {
			*a_params.retVal = true;
		}
	}
};

class OpenHostTitleFn : public FunctionHandler
{
public:
	void Call(const Params& a_params) override
	{
		CMP_MenuOpenHost(true);
		if (a_params.retVal) {
			*a_params.retVal = true;
		}
	}
};

class HostFn : public FunctionHandler
{
public:
	void Call(const Params& a_params) override
	{
		CMP_MenuOpenHost(false);
		if (a_params.retVal) {
			*a_params.retVal = true;
		}
	}
};

class DisconnectFn : public FunctionHandler
{
public:
	void Call(const Params& a_params) override
	{
		CMP_MenuDisconnect();
		if (a_params.retVal) {
			*a_params.retVal = true;
		}
	}
};

class IsConnectedFn : public FunctionHandler
{
public:
	void Call(const Params& a_params) override
	{
		if (a_params.retVal) {
			*a_params.retVal = CMP_MenuIsConnected();
		}
	}
};

void BindFn(Movie* a_movie, Value& a_obj, const char* a_name, FunctionHandler* a_handler)
{
	Value fn;
	a_movie->CreateFunction(&fn, a_handler);
	a_obj.SetMember(a_name, fn);
}

bool PauseModeActive(Movie* a_movie)
{
	Value pauseMode;
	if (!a_movie->GetVariable(std::addressof(pauseMode), "root.Menu_mc.PauseMode")) {
		return false;
	}
	return pauseMode.IsBoolean() && pauseMode.GetBoolean();
}

bool g_companionLoaded{ false };

bool EnsureCompanionSwf(Movie* a_movie, bool a_titleMenu)
{
	if (!a_movie || !a_movie->asMovieRoot) {
		return false;
	}

	const bool pause = PauseModeActive(a_movie);
	if (a_titleMenu ? pause : !pause) {
		return false;
	}

	if (g_companionLoaded) {
		return true;
	}

	auto* root = a_movie->asMovieRoot.get();
	Value rootObj;
	if (!root->GetVariable(std::addressof(rootObj), "root")) {
		return false;
	}

	Value existingLoader;
	if (rootObj.GetMember("cmp_loader", std::addressof(existingLoader)) && existingLoader.IsObject()) {
		g_companionLoaded = true;
		return true;
	}

	Value loader;
	root->CreateObject(&loader, "flash.display.Loader");

	Value urlName;
	root->CreateString(&urlName, kCompanionSwf);

	Value urlRequest;
	root->CreateObject(&urlRequest, "flash.net.URLRequest", &urlName, 1);

	rootObj.SetMember("cmp_loader", loader);

	if (!root->Invoke("root.cmp_loader.load", nullptr, &urlRequest, 1)) {
		REX::WARN("scaleform: cmp_loader.load failed");
		return false;
	}

	const bool parented = a_titleMenu
		? root->Invoke("root.addChild", nullptr, &loader, 1)
		: root->Invoke("root.Menu_mc.addChild", nullptr, &loader, 1);
	if (!parented) {
		REX::WARN("scaleform: companion addChild failed ({})", a_titleMenu ? "root" : "Menu_mc");
		return false;
	}

	g_companionLoaded = true;
	REX::INFO("scaleform: companion SWF loaded ({})", a_titleMenu ? "title" : "pause");
	return true;
}

void SyncCompanionSwf()
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}

	if (ui->GetMenuOpen("PauseMenu"sv)) {
		const auto menu = ui->GetMenu("PauseMenu");
		if (menu && menu->uiMovie) {
			EnsureCompanionSwf(menu->uiMovie.get(), false);
		}
		return;
	}

	if (ui->GetMenuOpen("MainMenu"sv)) {
		const auto menu = ui->GetMenu("MainMenu");
		if (menu && menu->uiMovie) {
			EnsureCompanionSwf(menu->uiMovie.get(), true);
		}
	}
}

bool ScaleformCallback(Movie* a_view, Value* /*a_f4seRoot*/)
{
	if (!a_view || !a_view->asMovieRoot) {
		return true;
	}
	auto* root = a_view->asMovieRoot.get();

	Value url;
	if (!root->GetVariable(std::addressof(url), "root.loaderInfo.url") || !url.IsString()) {
		return true;
	}
	if (std::string_view{ url.GetString() } != kHostSwf) {
		return true;
	}

	REX::INFO("scaleform: hooking {}", kHostSwf);

	Value rootObj;
	if (!root->GetVariable(std::addressof(rootObj), "root")) {
		REX::WARN("scaleform: root missing");
		return true;
	}

	Value codeObj;
	root->CreateObject(&codeObj);
	BindFn(a_view, codeObj, "OpenJoin", new OpenJoinFn());
	BindFn(a_view, codeObj, "OpenJoinTitle", new OpenJoinTitleFn());
	BindFn(a_view, codeObj, "OpenHost", new OpenHostFn());
	BindFn(a_view, codeObj, "OpenHostTitle", new OpenHostTitleFn());
	BindFn(a_view, codeObj, "Host", new HostFn());
	BindFn(a_view, codeObj, "Disconnect", new DisconnectFn());
	BindFn(a_view, codeObj, "IsConnected", new IsConnectedFn());
	codeObj.SetMember("buttonPos", 1.0);
	rootObj.SetMember("cmp", codeObj);

	if (PauseModeActive(a_view)) {
		EnsureCompanionSwf(a_view, false);
	} else {
		g_companionLoaded = false;
		EnsureCompanionSwf(a_view, true);
	}

	return true;
}

class CmpMenuOpenSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static CmpMenuOpenSink* GetSingleton()
	{
		static CmpMenuOpenSink sink;
		return std::addressof(sink);
	}

	RE::BSEventNotifyControl ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
	{
		if (!a_event.opening) {
			if (a_event.menuName == "MainMenu"sv || a_event.menuName == "PauseMenu"sv) {
				g_companionLoaded = false;
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return RE::BSEventNotifyControl::kContinue;
		}

		if (a_event.menuName == "PauseMenu"sv) {
			const auto menu = ui->GetMenu("PauseMenu");
			if (menu && menu->uiMovie) {
				EnsureCompanionSwf(menu->uiMovie.get(), false);
			}
		} else if (a_event.menuName == "MainMenu"sv) {
			const auto menu = ui->GetMenu("MainMenu");
			if (menu && menu->uiMovie) {
				EnsureCompanionSwf(menu->uiMovie.get(), true);
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}

private:
	CmpMenuOpenSink() = default;
};

bool g_scaleformRegistered{ false };
bool g_menuEventsRegistered{ false };

void CmpScaleformMenuTickImpl()
{
	SyncCompanionSwf();
}

}  // namespace

void CMP_InstallScaleformMenu()
{
	if (g_scaleformRegistered) {
		return;
	}
	const auto* scaleform = F4SE::GetScaleformInterface();
	if (!scaleform) {
		REX::WARN("scaleform: interface unavailable");
		return;
	}
	if (scaleform->Register("CommonwealthMP", ScaleformCallback)) {
		g_scaleformRegistered = true;
		REX::INFO("scaleform: registered (SWF {})", kCompanionSwf);
	} else {
		REX::WARN("scaleform: Register failed");
	}
}

void CMP_RegisterScaleformMenuEvents()
{
	if (g_menuEventsRegistered) {
		return;
	}
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}
	ui->RegisterSink(CmpMenuOpenSink::GetSingleton());
	g_menuEventsRegistered = true;
	REX::INFO("scaleform: MainMenu/PauseMenu open sink ready");
}

void CMP_ScaleformMenuTick()
{
	CmpScaleformMenuTickImpl();
}
