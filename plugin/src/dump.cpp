#include "pch.h"
#include "session.h"
#include "dump.h"
#include "crash.h"
#include "ghost.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string DumpPath()
{
	wchar_t* knownBuffer = nullptr;
	const auto knownResult = REX::W32::SHGetKnownFolderPath(
		REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, &knownBuffer);
	std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(
		knownBuffer, REX::W32::CoTaskMemFree);
	if (!knownPath || knownResult != 0) {
		return {};
	}
	std::filesystem::path path = knownPath.get();
	path /= std::format("My Games/{}/F4SE", F4SE::GetSaveFolderName());
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	path /= "CommonwealthMP.dump.txt";
	return path.string();
}

std::string Field(const char* raw)
{
	if (!raw || !raw[0]) {
		return "-";
	}
	std::string s = raw;
	for (char& c : s) {
		if (c == '\t' || c == '\r' || c == '\n') {
			c = ' ';
		}
	}
	return s;
}

std::string Field(std::string_view raw)
{
	if (raw.empty()) {
		return "-";
	}
	return Field(std::string(raw).c_str());
}

const char* PluginName(RE::TESForm* form)
{
	if (!form) {
		return "-";
	}
	auto* data = RE::TESDataHandler::GetSingleton();
	if (!data) {
		return "-";
	}
	const auto id = form->GetFormID();
	if ((id >> 24) == 0xFE) {
		const auto* file = data->LookupLoadedLightModByIndex(static_cast<std::uint16_t>((id >> 12) & 0xFFF));
		return file && file->filename[0] ? file->filename : "light";
	}
	const auto* file = data->LookupLoadedModByIndex(static_cast<std::uint8_t>(id >> 24));
	return file && file->filename[0] ? file->filename : "-";
}

const char* EditorId(RE::TESForm* form)
{
	if (!form) {
		return "";
	}
	if (auto* idle = form->As<RE::TESIdleForm>()) {
		if (idle->formEditorID.c_str() && idle->formEditorID.c_str()[0]) {
			return idle->formEditorID.c_str();
		}
	}
	if (auto* world = form->As<RE::TESWorldSpace>()) {
		if (world->editorID.c_str() && world->editorID.c_str()[0]) {
			return world->editorID.c_str();
		}
	}
	if (auto* race = form->As<RE::TESRace>()) {
		if (race->formEditorID.c_str() && race->formEditorID.c_str()[0]) {
			return race->formEditorID.c_str();
		}
	}
	const auto* edid = form->GetFormEditorID();
	return edid ? edid : "";
}

std::string DisplayName(RE::TESForm* form)
{
	if (!form) {
		return {};
	}
	if (auto* refr = form->As<RE::TESObjectREFR>()) {
		if (const auto* n = refr->GetDisplayFullName(); n && n[0]) {
			return n;
		}
	}
	return std::string(RE::TESFullName::GetFullName(*form));
}

bool SkipType(RE::ENUM_FORM_ID type)
{
	switch (type) {
	case RE::ENUM_FORM_ID::kNONE:
	case RE::ENUM_FORM_ID::kTES4:
	case RE::ENUM_FORM_ID::kGRUP:
	case RE::ENUM_FORM_ID::kSTAT:
	case RE::ENUM_FORM_ID::kSCOL:
	case RE::ENUM_FORM_ID::kMSTT:
	case RE::ENUM_FORM_ID::kGRAS:
	case RE::ENUM_FORM_ID::kTREE:
	case RE::ENUM_FORM_ID::kFLOR:
	case RE::ENUM_FORM_ID::kLTEX:
	case RE::ENUM_FORM_ID::kREFR:
	case RE::ENUM_FORM_ID::kACHR:
	case RE::ENUM_FORM_ID::kPMIS:
	case RE::ENUM_FORM_ID::kPARW:
	case RE::ENUM_FORM_ID::kPGRE:
	case RE::ENUM_FORM_ID::kPBEA:
	case RE::ENUM_FORM_ID::kPFLA:
	case RE::ENUM_FORM_ID::kPCON:
	case RE::ENUM_FORM_ID::kPBAR:
	case RE::ENUM_FORM_ID::kPHZD:
	case RE::ENUM_FORM_ID::kLAND:
	case RE::ENUM_FORM_ID::kNAVM:
	case RE::ENUM_FORM_ID::kNAVI:
	case RE::ENUM_FORM_ID::kINFO:
	case RE::ENUM_FORM_ID::kTLOD:
	case RE::ENUM_FORM_ID::kTOFT:
	case RE::ENUM_FORM_ID::kRGDL:
	case RE::ENUM_FORM_ID::kRFGP:
	case RE::ENUM_FORM_ID::kLAYR:
	case RE::ENUM_FORM_ID::kSCCO:
	case RE::ENUM_FORM_ID::kNOCM:
	case RE::ENUM_FORM_ID::kLSPR:
	case RE::ENUM_FORM_ID::kOVIS:
		return true;
	default:
		return false;
	}
}

void HexU64(std::ofstream& out, std::uint64_t v)
{
	char buf[32]{};
	std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(v));
	out << buf;
}

void Hex32(std::ofstream& out, std::uint32_t v)
{
	char buf[16]{};
	std::snprintf(buf, sizeof(buf), "%08X", v);
	out << buf;
}

void WriteFormRow(std::ofstream& out, RE::TESForm* form, std::string extra)
{
	if (!form) {
		return;
	}
	Hex32(out, form->GetFormID());
	out << '\t' << Field(form->GetFormTypeString())
		<< '\t' << Field(EditorId(form))
		<< '\t' << Field(PluginName(form))
		<< '\t' << "-";
	if (!extra.empty()) {
		out << '\t' << extra;
	}
	out << '\n';
}

std::string ExtraFor(RE::TESForm* form)
{
	if (!form) {
		return {};
	}
	char buf[512]{};
	if (auto* idle = form->As<RE::TESIdleForm>()) {
		std::snprintf(buf, sizeof(buf), "graph=%s event=%s file=%s parent=%08X",
			idle->behaviorGraphName.c_str() ? idle->behaviorGraphName.c_str() : "-",
			idle->animEventName.c_str() ? idle->animEventName.c_str() : "-",
			idle->animFileName.c_str() ? idle->animFileName.c_str() : "-",
			idle->parentIdle ? idle->parentIdle->GetFormID() : 0);
		return Field(buf);
	}
	if (auto* world = form->As<RE::TESWorldSpace>()) {
		std::snprintf(buf, sizeof(buf), "parent=%08X",
			world->parentWorld ? world->parentWorld->GetFormID() : 0);
		return Field(buf);
	}
	if (auto* race = form->As<RE::TESRace>()) {
		std::snprintf(buf, sizeof(buf), "graph0=%s graph1=%s project0=%s project1=%s",
			race->behaviorGraph[0].GetModel() ? race->behaviorGraph[0].GetModel() : "-",
			race->behaviorGraph[1].GetModel() ? race->behaviorGraph[1].GetModel() : "-",
			race->behaviorGraphProjectName[0].c_str() ? race->behaviorGraphProjectName[0].c_str() : "-",
			race->behaviorGraphProjectName[1].c_str() ? race->behaviorGraphProjectName[1].c_str() : "-");
		return Field(buf);
	}
	if (auto* cell = form->As<RE::TESObjectCELL>()) {
		std::snprintf(buf, sizeof(buf), "interior=%d attached=%d state=%u",
			cell->IsInterior() ? 1 : 0,
			cell->IsAttached() ? 1 : 0,
			static_cast<unsigned>(cell->cellState));
		return Field(buf);
	}
	if (auto* npc = form->As<RE::TESNPC>()) {
		std::snprintf(buf, sizeof(buf), "unique=%d race=%08X",
			npc->IsUnique() ? 1 : 0,
			npc->GetFormRace() ? npc->GetFormRace()->GetFormID() : 0);
		return Field(buf);
	}
	if (auto* movt = form->As<RE::BGSMovementType>()) {
		return Field(movt->movementTypeData.typeName.c_str());
	}
	return {};
}

void WriteRel(std::ofstream& out, const char* name, REL::ID id)
{
	if (id.id() == 0) {
		out << name << "\t0\t-\t-\n";
		return;
	}
	const auto rva = static_cast<std::uint64_t>(id.offset());
	const auto va = static_cast<std::uint64_t>(id.address());
	out << name << '\t' << id.id() << '\t';
	HexU64(out, rva);
	out << '\t';
	HexU64(out, va);
	out << '\n';
}

void WriteModule(std::ofstream& out, const char* name)
{
	const auto mod = REX::FModule::GetLoadedModule(name);
	if (!mod.GetBaseAddress()) {
		out << name << "\t-\t-\t-\n";
		return;
	}
	out << Field(std::filesystem::path(mod.GetFileName()).filename().string())
		<< '\t';
	HexU64(out, mod.GetBaseAddress());
	out << '\t' << Field(mod.GetFileVersion().string())
		<< '\t' << Field(mod.GetFileName()) << '\n';
}

void WriteGraphVars(std::ofstream& out, RE::TESObjectREFR* refr)
{
	if (!refr) {
		out << "# no refr\n";
		return;
	}
	static constexpr const char* kNames[] = {
		"Speed",
		"SpeedSampled",
		"Direction",
		"TurnDelta",
		"Pitch",
		"IsSprinting",
		"IsSneaking",
		"IsFirstPerson",
		"IsBlocking",
		"IsRecoiling",
		"IsStaggering",
		"IsAttacking",
		"IsAttackReady",
		"IsEquipping",
		"IsUnequipping",
		"IsInJumpState",
		"bInJumpState",
		"bWantGait",
		"bForceIdleStop",
		"bMotionDriven",
		"bAnimationDriven",
		"bAllowRotation",
		"iSyncSprintState",
		"iSyncIdleLocomotion",
		"iState",
		"iWantGait",
		"iRightHandType",
		"iLeftHandType",
		"weaponSpeedMult",
		"WeaponSpeedMult",
		"AimHeadingMax",
		"AimPitchMax",
		"EquippedWeaponType"
	};
	for (const auto* name : kNames) {
		const RE::BSFixedString var{ name };
		float f = 0.f;
		std::int32_t i = 0;
		bool b = false;
		const bool gotF = refr->GetGraphVariableImplFloat(var, f);
		const bool gotI = refr->GetGraphVariableImplInt(var, i);
		const bool gotB = refr->GetGraphVariableImplBool(var, b);
		if (!gotF && !gotI && !gotB) {
			out << name << "\tmissing\n";
			continue;
		}
		out << name << '\t';
		if (gotF) {
			out << "f=" << f;
		}
		if (gotI) {
			if (gotF) {
				out << ' ';
			}
			out << "i=" << i;
		}
		if (gotB) {
			if (gotF || gotI) {
				out << ' ';
			}
			out << "b=" << (b ? 1 : 0);
		}
		out << '\n';
	}
}

struct FormRowCtx {
	std::ofstream* out{};
	RE::TESForm* form{};
};

void WriteFormRowSeh(void* p)
{
	auto* c = static_cast<FormRowCtx*>(p);
	WriteFormRow(*c->out, c->form, ExtraFor(c->form));
}

struct NamedFormCtx {
	std::ofstream* out{};
	RE::TESForm* form{};
	const char* edid{};
};

void WriteNamedSeh(void* p)
{
	auto* c = static_cast<NamedFormCtx*>(p);
	if (!c->out || !c->form) {
		return;
	}
	const auto type = c->form->GetFormType();
	const char* typeStr = "-";
	if (type < RE::ENUM_FORM_ID::kTotal) {
		typeStr = RE::TESForm::GetFormTypeString(type);
	}
	Hex32(*c->out, c->form->GetFormID());
	*c->out << '\t' << Field(typeStr)
		<< '\t' << Field(c->edid)
		<< '\t' << Field(PluginName(c->form))
		<< "\t-\n";
}

struct GraphCtx {
	std::ofstream* out{};
	RE::TESObjectREFR* refr{};
};

void WriteGraphSeh(void* p)
{
	auto* c = static_cast<GraphCtx*>(p);
	WriteGraphVars(*c->out, c->refr);
}

void DumpActors(std::ofstream& out, const char* label, const RE::BSTArray<RE::ActorHandle>& handles)
{
	out << "# " << label << " count=" << handles.size() << '\n';
	out << "formid\ttype\tedid\tplugin\tname\tbase\tcell\t3d\tx\ty\tz\n";
	for (const auto& handle : handles) {
		auto actor = handle.get();
		if (!actor) {
			continue;
		}
		FormRowCtx ctx{ &out, actor.get() };
		CMP_SehCall("dump_actor", WriteFormRowSeh, &ctx);
	}
}

}  // namespace

std::string CMP_DumpLive()
{
	CMP_CrashNote("cmp_dump");
	const auto path = DumpPath();
	if (path.empty()) {
		REX::ERROR("cmp_dump: no Documents path");
		return {};
	}

	std::ofstream out(path, std::ios::trunc);
	if (!out) {
		REX::ERROR("cmp_dump: cannot write {}", path);
		return {};
	}

	out << "# CommonwealthMP live dump. TSV. Copy this file into the repo as data/dumps/live.txt\n";
	out << "# For research\n";

	const auto exe = REX::FModule::GetExecutingModule();
	out << "\n## runtime\n";
	out << "plugin\t" << Field(F4SE::GetPluginName()) << '\t' << Field(F4SE::GetPluginVersion().string()) << '\n';
	out << "f4se\t" << Field(F4SE::GetF4SEVersion().string()) << '\n';
	out << "save_folder\t" << Field(F4SE::GetSaveFolderName()) << '\n';
	out << "exe_version\t" << Field(exe.GetFileVersion().string()) << '\n';
	out << "exe_base\t";
	HexU64(out, exe.GetBaseAddress());
	out << '\n';
	out << "exe_path\t" << Field(exe.GetFileName()) << '\n';
	out << "ghost_source\t";
	Hex32(out, CMP_Session().settings.ghostSourceForm);
	out << '\n';
	out << "commonwealth_worldspace\t";
	Hex32(out, cmp::kCommonwealthWorldspace);
	out << '\n';

	out << "\n## modules\n";
	out << "name\tbase\tversion\tpath\n";
	WriteModule(out, "Fallout4.exe");
	WriteModule(out, "CommonwealthMP.dll");
	WriteModule(out, "f4se_1_11_137.dll");
	WriteModule(out, "f4se_1_10_163.dll");
	WriteModule(out, "steam_api64.dll");
	WriteModule(out, "version.dll");

	out << "\n## rel\n";
	out << "name\tid\trva\tva\n";
	WriteRel(out, "TESDataHandler::Singleton", RE::ID::TESDataHandler::Singleton);
	WriteRel(out, "TESDataHandler::CreateReferenceAtLocation", RE::ID::TESDataHandler::CreateReferenceAtLocation);
	WriteRel(out, "TESForm::AllForms", RE::ID::TESForm::AllForms);
	WriteRel(out, "TESForm::AllFormsByEditorID", RE::ID::TESForm::AllFormsByEditorID);
	WriteRel(out, "TESForm::GetFile", RE::ID::TESForm::GetFile);
	WriteRel(out, "TESForm::GetFormEnumString", RE::ID::TESForm::GetFormEnumString);
	WriteRel(out, "PlayerCharacter::Singleton", RE::ID::PlayerCharacter::Singleton);
	WriteRel(out, "ProcessLists::Singleton", RE::ID::ProcessLists::Singleton);
	WriteRel(out, "ActorEquipManager::Singleton", RE::ID::ActorEquipManager::Singleton);
	WriteRel(out, "ActorEquipManager::EquipObject", RE::ID::ActorEquipManager::EquipObject);
	WriteRel(out, "SCRIPT_FUNCTION::ConsoleFunctions", RE::ID::SCRIPT_FUNCTION::ConsoleFunctions);
	WriteRel(out, "SCRIPT_FUNCTION::ScriptFunctions", RE::ID::SCRIPT_FUNCTION::ScriptFunctions);
	WriteRel(out, "TESObjectREFR::GetCurrentLocation", RE::ID::TESObjectREFR::GetCurrentLocation);
	WriteRel(out, "TESObjectREFR::SetLocationOnReference", RE::ID::TESObjectREFR::SetLocationOnReference);
	WriteRel(out, "IAnimationGraphManagerHolder::SetGraphVariableFloat", RE::ID::IAnimationGraphManagerHolder::SetGraphVariableFloat);
	WriteRel(out, "IAnimationGraphManagerHolder::SetGraphVariableBool", RE::ID::IAnimationGraphManagerHolder::SetGraphVariableBool);
	WriteRel(out, "IAnimationGraphManagerHolder::SetGraphVariableInt", RE::ID::IAnimationGraphManagerHolder::SetGraphVariableInt);
	WriteRel(out, "Actor::GetDesiredSpeed", RE::ID::Actor::GetDesiredSpeed);
	WriteRel(out, "Actor::IsSneaking", RE::ID::Actor::IsSneaking);
	WriteRel(out, "Actor::IsJumping", RE::ID::Actor::IsJumping);
	WriteRel(out, "Actor::UpdateSprinting", RE::ID::Actor::UpdateSprinting);
	WriteRel(out, "Actor::SetHeading", RE::ID::Actor::SetHeading);
	WriteRel(out, "Actor::Move", RE::ID::Actor::Move);

	auto* data = RE::TESDataHandler::GetSingleton();
	out << "\n## plugins\n";
	out << "index\tsmall\tlight\tfilename\n";
	if (data) {
		for (auto* file : data->compiledFileCollection.files) {
			if (!file) {
				continue;
			}
			out << static_cast<unsigned>(file->GetCompileIndex()) << "\t"
				<< file->GetSmallFileCompileIndex() << '\t'
				<< (file->IsLight() ? 1 : 0) << '\t'
				<< Field(file->filename) << '\n';
		}
		for (auto* file : data->compiledFileCollection.smallFiles) {
			if (!file) {
				continue;
			}
			out << static_cast<unsigned>(file->GetCompileIndex()) << "\t"
				<< file->GetSmallFileCompileIndex() << '\t'
				<< (file->IsLight() ? 1 : 0) << '\t'
				<< Field(file->filename) << '\n';
		}
	} else {
		out << "# TESDataHandler missing\n";
	}

	out << "\n## form_counts\n";
	out << "type\tcount\n";
	if (data) {
		for (int i = 0; i < static_cast<int>(RE::ENUM_FORM_ID::kTotal); ++i) {
			const auto type = static_cast<RE::ENUM_FORM_ID>(i);
			out << Field(RE::TESForm::GetFormTypeString(type)) << '\t'
				<< data->formArrays[i].size() << '\n';
		}
	}

	out << "\n## named_forms\n";
	out << "formid\ttype\tedid\tplugin\tname\n";
	{
		const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
		RE::BSAutoReadLock l{ lock };
		if (map) {
			for (const auto& kv : *map) {
				auto* form = kv.second;
				if (!form) {
					continue;
				}
				NamedFormCtx ctx{ &out, form, kv.first.c_str() };
				CMP_SehCall("named_form", WriteNamedSeh, &ctx);
			}
		} else {
			out << "# AllFormsByEditorID missing\n";
		}
	}

	if (data) {
		for (int i = 0; i < static_cast<int>(RE::ENUM_FORM_ID::kTotal); ++i) {
			const auto type = static_cast<RE::ENUM_FORM_ID>(i);
			if (SkipType(type)) {
				continue;
			}
			auto& arr = data->formArrays[i];
			out << "\n## forms_" << Field(RE::TESForm::GetFormTypeString(type))
				<< " count=" << arr.size() << '\n';
			out << "formid\ttype\tedid\tplugin\tname\textra\n";
			for (auto* form : arr) {
				FormRowCtx ctx{ &out, form };
				CMP_SehCall("form_row", WriteFormRowSeh, &ctx);
			}
		}
	}

	out << "\n## console\n";
	out << "name\tshort\thelp\texec_rva\n";
	const auto exeBase = exe.GetBaseAddress();
	for (auto& cmd : RE::SCRIPT_FUNCTION::GetConsoleFunctions()) {
		if (!cmd.functionName || !cmd.functionName[0]) {
			continue;
		}
		const auto fn = reinterpret_cast<std::uintptr_t>(cmd.executeFunction);
		out << Field(cmd.functionName) << '\t'
			<< Field(cmd.shortName) << '\t'
			<< Field(cmd.helpString) << '\t';
		if (fn && fn >= exeBase) {
			HexU64(out, fn - exeBase);
		} else {
			out << '-';
		}
		out << '\n';
	}

	out << "\n## script\n";
	out << "name\tshort\n";
	for (auto& cmd : RE::SCRIPT_FUNCTION::GetScriptFunctions()) {
		if (!cmd.functionName || !cmd.functionName[0]) {
			continue;
		}
		out << Field(cmd.functionName) << '\t' << Field(cmd.shortName) << '\n';
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	out << "\n## player\n";
	if (!player) {
		out << "# no PlayerCharacter\n";
	} else {
		auto* cell = player->GetParentCell();
		auto* npc = player->GetNPC();
		const auto pos = player->GetPosition();
		out << "formid\t";
		Hex32(out, player->GetFormID());
		out << '\n';
		out << "base\t";
		Hex32(out, npc ? npc->GetFormID() : 0);
		out << '\n';
		out << "race\t";
		Hex32(out, npc && npc->GetFormRace() ? npc->GetFormRace()->GetFormID() : 0);
		out << '\n';
		out << "cell\t";
		Hex32(out, cell ? cell->GetFormID() : 0);
		out << '\t' << Field(cell ? EditorId(cell) : "")
			<< '\t' << (cell && cell->IsInterior() ? "interior" : "exterior") << '\n';
		std::uint32_t world = 0;
		if (cell && !cell->IsInterior() && cell->worldSpace) {
			world = cell->worldSpace->GetFormID();
		}
		out << "worldspace\t";
		Hex32(out, world);
		out << '\n';
		out << "xyz\t" << pos.x << '\t' << pos.y << '\t' << pos.z << '\n';
		out << "angle\t" << player->data.angle.x << '\t' << player->data.angle.y << '\t' << player->data.angle.z << '\n';
		out << "3d\t" << (player->Get3D() ? 1 : 0) << '\n';
		if (auto* loc = player->GetCurrentLocation()) {
			out << "location\t";
			Hex32(out, loc->GetFormID());
			out << '\t' << Field(EditorId(loc)) << '\t' << Field(DisplayName(loc)) << '\n';
		}
		out << "\n## player_graph\n";
		out << "name\tvalue\n";
		{
			GraphCtx ctx{ &out, player };
			CMP_SehCall("player_graph", WriteGraphSeh, &ctx);
		}

		if (cell) {
			out << "\n## player_cell_refs\n";
			out << "formid\ttype\tedid\tplugin\tname\textra\n";
			int n = 0;
			cell->ForEachReference([&](RE::TESObjectREFR* refr) {
				if (!refr || n >= 2500) {
					return RE::BSContainer::ForEachResult::kStop;
				}
				++n;
				FormRowCtx ctx{ &out, refr };
				CMP_SehCall("cell_ref", WriteFormRowSeh, &ctx);
				return RE::BSContainer::ForEachResult::kContinue;
			});
			out << "# player_cell_refs_written=" << n << '\n';
		}
	}

	if (auto* lists = RE::ProcessLists::GetSingleton()) {
		out << "\n## actors_high\n";
		DumpActors(out, "high", lists->highActorHandles);
		out << "\n## actors_middle_high\n";
		DumpActors(out, "middleHigh", lists->middleHighActorHandles);
	}

	out << "\n## ghosts\n";
	{
		auto& s = CMP_Session();
		std::lock_guard lock(s.mutex);
		out << "joined=" << (s.net.joined ? 1 : 0)
			<< " peer=" << s.net.myPeerId
			<< " host=" << (s.net.isHost ? 1 : 0)
			<< " ghosts=" << s.ghosts.byPeer.size() << '\n';
		for (const auto& [peer, handle] : s.ghosts.byPeer) {
			auto refr = handle.get();
			out << "peer=" << peer << '\t';
			if (!refr) {
				out << "missing\n";
				continue;
			}
			Hex32(out, refr->GetFormID());
			out << '\t' << (refr->Get3D() ? "3d" : "no3d") << '\n';
		}
	}

	out.flush();
	REX::INFO("cmp_dump wrote {}", path);
	return path;
}
