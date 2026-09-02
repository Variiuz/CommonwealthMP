#include "pch.h"
#include "menu/internal.h"
#include "menu.h"

#include "Scaleform/G/GFx_Value.h"

#include "REX/FModule.h"

#include <string_view>

namespace cmp_menu {

using GFxValue = Scaleform::GFx::Value;

bool GfxMemberString(const GFxValue& a_obj, const char* a_member, std::string& a_out)
{
	GFxValue val;
	if (!a_obj.GetMember(a_member, &val) || !val.IsString()) {
		return false;
	}
	a_out = val.GetString();
	return true;
}

bool VersionHtmlHasVanilla(std::string_view a_html)
{
	return a_html.find("Fallout") != std::string_view::npos
		|| a_html.find("F4SE") != std::string_view::npos;
}

std::string TrimCopyLocal(std::string_view a_text)
{
	std::string out(a_text);
	while (!out.empty() && (out.front() == ' ' || out.front() == '\t')) {
		out.erase(out.begin());
	}
	while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
		out.pop_back();
	}
	return out;
}

std::string StripHtmlToText(std::string a_html)
{
	for (size_t pos = 0; (pos = a_html.find('<', pos)) != std::string::npos;) {
		const size_t end = a_html.find('>', pos);
		if (end == std::string::npos) {
			break;
		}
		const auto tag = a_html.substr(pos, end - pos + 1);
		if (tag.find("<br") == 0 || tag.find("<BR") == 0) {
			a_html.replace(pos, end - pos + 1, "\n");
			pos += 1;
			continue;
		}
		a_html.erase(pos, end - pos + 1);
	}
	return TrimCopyLocal(a_html);
}

std::string FindLineContaining(std::string_view a_text, std::string_view a_needle)
{
	size_t start = 0;
	while (start < a_text.size()) {
		size_t end = a_text.find('\n', start);
		if (end == std::string_view::npos) {
			end = a_text.size();
		}
		const auto line = TrimCopyLocal(a_text.substr(start, end - start));
		if (line.find(a_needle) != std::string_view::npos) {
			return line;
		}
		start = end + 1;
	}
	return {};
}

std::string DefaultGameVersionLine()
{
	const auto exe = REX::FModule::GetExecutingModule();
	return std::string("Fallout 4 ") + exe.GetFileVersion().string();
}

std::string DefaultF4seVersionLine()
{
	return std::string("F4SE ") + F4SE::GetF4SEVersion().string();
}

std::string BuildVersionText()
{
	const std::string gameLine = DefaultGameVersionLine();
	const std::string f4seLine = DefaultF4seVersionLine();
	const std::string cmpLine = std::string(kCmpVersionMarker) + F4SE::GetPluginVersion().string();
	return gameLine + "\n" + f4seLine + "\n" + cmpLine;
}

bool VersionTextLooksCorrect(std::string_view a_text)
{
	if (a_text.find(kCmpVersionMarker) == std::string_view::npos) {
		return false;
	}
	std::size_t lines = 1;
	for (const char c : a_text) {
		if (c == '\n') {
			++lines;
		}
	}
	return lines >= 3;
}

void GetStageSize(RE::MainMenu* a_menu, double& a_width, double& a_height)
{
	a_width = 1920.0;
	a_height = 1080.0;
	if (!a_menu || !a_menu->uiMovie) {
		return;
	}
	GFxValue stageW;
	GFxValue stageH;
	if (a_menu->uiMovie->GetVariable(&stageW, "root.stage.stageWidth") && stageW.IsNumber()) {
		a_width = stageW.GetNumber();
	}
	if (a_menu->uiMovie->GetVariable(&stageH, "root.stage.stageHeight") && stageH.IsNumber()) {
		a_height = stageH.GetNumber();
	}
}

void EnsureVersionOnRoot(RE::MainMenu* a_menu, GFxValue& a_rootObj)
{
	if (!a_menu || !a_menu->versionText) {
		return;
	}
	GFxValue& versionObj = *a_menu->versionText;
	GFxValue onRoot;
	if (a_rootObj.Invoke("contains", &onRoot, &versionObj, 1) && onRoot.IsBoolean() && onRoot.GetBoolean()) {
		return;
	}
	a_rootObj.Invoke("addChild", nullptr, &versionObj, 1);
}

void AnchorVersionBottomRight(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->versionText) {
		return;
	}

	double stageW = 0.0;
	double stageH = 0.0;
	GetStageSize(a_menu, stageW, stageH);

	const double x = stageW - kVersionMarginX - kVersionFieldWidth;
	const double y = stageH - kVersionMarginY - (kVersionLineHeight * 3.0);

	auto* versionText = a_menu->versionText.get();
	if (a_menu->uiMovie) {
		GFxValue rootObj;
		if (a_menu->uiMovie->GetVariable(&rootObj, "root")) {
			EnsureVersionOnRoot(a_menu, rootObj);
		}
	}
	versionText->SetMember("multiline", GFxValue(true));
	versionText->SetMember("wordWrap", GFxValue(false));
	versionText->SetMember("textAlign", GFxValue("right"));
	versionText->SetMember("autoSize", GFxValue("none"));
	versionText->SetMember("width", GFxValue(kVersionFieldWidth));
	versionText->SetMember("_x", GFxValue(x));
	versionText->SetMember("_y", GFxValue(y));
	versionText->SetMember("visible", GFxValue(true));
}

void StampVersion(RE::MainMenu* menu)
{
	if (!menu || !menu->versionText || IsPauseMainMenu(menu)) {
		return;
	}

	const std::string label = std::string(kCmpVersionMarker) + F4SE::GetPluginVersion().string();
	std::string existing;
	if (!GfxMemberString(*menu->versionText, "text", existing)) {
		GfxMemberString(*menu->versionText, "htmlText", existing);
	}

	if (VersionTextLooksCorrect(existing)) {
		AnchorVersionBottomRight(menu);
		g_stamp = CMP_VersionStamp();
		return;
	}

	if (!VersionHtmlHasVanilla(existing)) {
		if (g_stampWait < kVersionStampWaitMax) {
			++g_stampWait;
			return;
		}
	}

	const std::string text = BuildVersionText();
	menu->versionText->SetMember("htmlText", GFxValue(""));
	menu->versionText->SetMember("text", GFxValue(text.c_str()));
	AnchorVersionBottomRight(menu);
	g_stamp = CMP_VersionStamp();
	REX::INFO("MainMenu version stamp anchored bottom-right {}", label);
}

void TryStampMainMenuVersion()
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui || !ui->GetMenuOpen("MainMenu"sv)) {
		return;
	}
	const auto ptr = ui->GetMenu<RE::MainMenu>();
	auto* mm = ptr.get();
	if (!mm || !MainMenuReady(mm) || IsPauseMainMenu(mm)) {
		return;
	}

	if (mm->versionText) {
		std::string existing;
		if (GfxMemberString(*mm->versionText, "text", existing)
			|| GfxMemberString(*mm->versionText, "htmlText", existing)) {
			if (VersionTextLooksCorrect(existing)) {
				AnchorVersionBottomRight(mm);
				return;
			}
		}
	}

	StampVersion(mm);
}

}  // namespace cmp_menu
