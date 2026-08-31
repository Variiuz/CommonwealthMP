#include "pch.h"
#include "cmp.h"

namespace {

void PrintLine(const std::string& s)
{
	CMP_Print(s);
}

void DumpForm(std::uint32_t id)
{
	auto* form = RE::TESForm::GetFormByID(id);
	if (!form) {
		REX::INFO("cmp_probeforms {:08X} MISSING", id);
		return;
	}
	auto* npc = form->As<RE::TESNPC>();
	REX::INFO("cmp_probeforms {:08X} type={} edid={} unique={} race={:08X}",
		id,
		RE::TESForm::GetFormTypeString(form->GetFormType()),
		form->GetFormEditorID(),
		npc ? npc->IsUnique() : false,
		npc && npc->GetFormRace() ? npc->GetFormRace()->GetFormID() : 0);
}

bool ExecStatus(
	const RE::SCRIPT_PARAMETER*,
	const char*,
	RE::TESObjectREFR*,
	RE::TESObjectREFR*,
	RE::Script*,
	RE::ScriptLocals*,
	float& result,
	std::uint32_t&)
{
	PrintLine(CMP_StatusText());
	const auto pointer = CMP_PointerText();
	RE::SendHUDMessage::ShowHUDMessage(pointer.c_str(), "", false, false);
	result = 0.f;
	return true;
}

bool ExecGoto(
	const RE::SCRIPT_PARAMETER*,
	const char*,
	RE::TESObjectREFR*,
	RE::TESObjectREFR*,
	RE::Script*,
	RE::ScriptLocals*,
	float& result,
	std::uint32_t&)
{
	CMP_GotoNearest();
	result = 0.f;
	return true;
}

bool ExecLeave(
	const RE::SCRIPT_PARAMETER*,
	const char*,
	RE::TESObjectREFR*,
	RE::TESObjectREFR*,
	RE::Script*,
	RE::ScriptLocals*,
	float& result,
	std::uint32_t&)
{
	CMP_Leave();
	CMP_DespawnGhosts();
	PrintLine("cmp_leave: disconnected");
	result = 0.f;
	return true;
}

bool ExecLobby(
	const RE::SCRIPT_PARAMETER*,
	const char*,
	RE::TESObjectREFR*,
	RE::TESObjectREFR*,
	RE::Script*,
	RE::ScriptLocals*,
	float& result,
	std::uint32_t&)
{
	PrintLine(CMP_ProbeSteamLobby());
	result = 0.f;
	return true;
}

bool ExecProbe(
	const RE::SCRIPT_PARAMETER*,
	const char*,
	RE::TESObjectREFR*,
	RE::TESObjectREFR*,
	RE::Script*,
	RE::ScriptLocals*,
	float& result,
	std::uint32_t&)
{
	CMP_ProbeForms();
	PrintLine("cmp_probeforms: wrote TESForm types to CommonwealthMP.log");
	result = 0.f;
	return true;
}

bool ExecDump(
	const RE::SCRIPT_PARAMETER*,
	const char*,
	RE::TESObjectREFR*,
	RE::TESObjectREFR*,
	RE::Script*,
	RE::ScriptLocals*,
	float& result,
	std::uint32_t&)
{
	const auto path = CMP_DumpLive();
	if (path.empty()) {
		PrintLine("cmp_dump: failed (see CommonwealthMP.log)");
	} else {
		PrintLine(std::string("cmp_dump: ") + path);
		PrintLine("copy that file to the repo as data/dumps/live.txt");
	}
	result = 0.f;
	return true;
}

RE::SCRIPT_PARAMETER g_joinParams[5]{
	{ "A", RE::SCRIPT_PARAM_TYPE::kInt, true },
	{ "B", RE::SCRIPT_PARAM_TYPE::kInt, true },
	{ "C", RE::SCRIPT_PARAM_TYPE::kInt, true },
	{ "D", RE::SCRIPT_PARAM_TYPE::kInt, true },
	{ "Port", RE::SCRIPT_PARAM_TYPE::kInt, true }
};

bool ExecJoin(
	const RE::SCRIPT_PARAMETER* params,
	const char* compiled,
	RE::TESObjectREFR* ref,
	RE::TESObjectREFR* container,
	RE::Script* script,
	RE::ScriptLocals* locals,
	float& result,
	std::uint32_t& offset)
{
	auto& s = CMP_Session();
	std::int32_t a = 127;
	std::int32_t b = 0;
	std::int32_t c = 0;
	std::int32_t d = 1;
	std::int32_t port = s.settings.port;
	if (params && compiled) {
		RE::Script::ParseParameters(params, compiled, offset, ref, container, script, locals, &a, &b, &c, &d, &port);
	}
	if (port <= 0 || port > 65535) {
		port = cmp::kDefaultPort;
	}
	const auto host = std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) + "." + std::to_string(d);
	CMP_Join(host, static_cast<std::uint16_t>(port));
	result = 0.f;
	return true;
}

bool ExecQuery(
	const RE::SCRIPT_PARAMETER* params,
	const char* compiled,
	RE::TESObjectREFR* ref,
	RE::TESObjectREFR* container,
	RE::Script* script,
	RE::ScriptLocals* locals,
	float& result,
	std::uint32_t& offset)
{
	auto& s = CMP_Session();
	std::int32_t a = 127;
	std::int32_t b = 0;
	std::int32_t c = 0;
	std::int32_t d = 1;
	std::int32_t port = s.settings.port;
	if (params && compiled) {
		RE::Script::ParseParameters(params, compiled, offset, ref, container, script, locals, &a, &b, &c, &d, &port);
	}
	if (port <= 0 || port > 65535) {
		port = cmp::kDefaultPort;
	}
	const auto host = std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) + "." + std::to_string(d);
	CMP_QueryStart(host, static_cast<std::uint16_t>(port));
	SessionQueryResult r;
	for (int i = 0; i < 50; ++i) {
		if (CMP_QueryPoll(r)) {
			break;
		}
		REX::W32::Sleep(40);
	}
	if (r.ok) {
		std::string line = "cmp_query host yes";
		if (r.info.serverName[0]) {
			line += std::string(" name=") + r.info.serverName;
		}
		line += " loc=" + std::to_string(r.info.hostLocationFormId)
			+ " pose=(" + std::to_string(static_cast<int>(r.info.hostX)) + ","
			+ std::to_string(static_cast<int>(r.info.hostY)) + ","
			+ std::to_string(static_cast<int>(r.info.hostZ)) + ") clients="
			+ std::to_string(r.info.clientCount) + "/" + std::to_string(r.info.maxPlayers);
		if (r.info.motd[0]) {
			line += std::string(" motd=") + r.info.motd;
		}
		PrintLine(line);
	} else {
		PrintLine(std::string("cmp_query failed: ") + (r.error.empty() ? "timeout" : r.error));
	}
	result = 0.f;
	return true;
}

void Steal(const char* vanillaName, const char* newName, const char* help, RE::SCRIPT_FUNCTION::ExecuteFunction_t* fn, RE::SCRIPT_PARAMETER* params = nullptr, std::uint16_t count = 0)
{
	auto* cmd = RE::SCRIPT_FUNCTION::LocateConsoleCommand(vanillaName);
	if (!cmd) {
		REX::WARN("No console command '{}' to steal for {}", vanillaName, newName);
		return;
	}
	cmd->functionName = newName;
	cmd->shortName = "";
	cmd->helpString = help;
	cmd->executeFunction = fn;
	cmd->referenceFunction = false;
	if (params && count) {
		cmd->paramCount = count;
		cmd->parameters = params;
	} else {
		cmd->SetParameters();
	}
	REX::INFO("Registered console {}", newName);
}

}  // namespace

void CMP_ProbeForms()
{
	DumpForm(0x7);
	DumpForm(0x20593);
	DumpForm(0x1D323);
	DumpForm(0xBB873);
	DumpForm(0xAA78E);

	const auto sourceId = CMP_Session().settings.ghostSourceForm;
	auto* source = RE::TESForm::GetFormByID<RE::TESNPC>(sourceId);
	if (source) {
		REX::INFO("cmp_probeforms ghost SourceForm {:08X} type={} unique={} race={:08X}",
			source->GetFormID(),
			RE::TESForm::GetFormTypeString(source->GetFormType()),
			source->IsUnique(),
			source->GetFormRace() ? source->GetFormRace()->GetFormID() : 0);
	} else {
		REX::ERROR("cmp_probeforms ghost SourceForm {:08X} MISSING", sourceId);
	}

	auto* factory = RE::ConcreteFormFactory<RE::TESNPC>::GetFormFactory();
	REX::INFO("cmp_probeforms ConcreteFormFactory<TESNPC>={}", factory ? "ok" : "MISSING");

	int npcCount = 0;
	auto* data = RE::TESDataHandler::GetSingleton();
	if (data) {
		npcCount = static_cast<int>(data->GetFormArray<RE::TESNPC>().size());
		const auto* file = data->LookupModByName("CommonwealthMP.esp");
		if (file) {
			REX::INFO("cmp_probeforms CommonwealthMP.esp loaded index={:02X} light={} (optional; ghosts use runtime clones)",
				file->GetCompileIndex(), file->IsLight());
		} else {
			REX::INFO("cmp_probeforms CommonwealthMP.esp not loaded (ok; ghosts clone SourceForm)");
		}
	}
	REX::INFO("cmp_probeforms TESNPC array count={}", npcCount);

	auto* remote = RE::TESForm::GetFormByEditorID<RE::TESNPC>(RE::BSFixedString("CMP_RemotePlayer"));
	if (remote) {
		REX::INFO("cmp_probeforms CMP_RemotePlayer {:08X} (legacy ESP; unused for spawn)",
			remote->GetFormID());
	} else {
		REX::INFO("cmp_probeforms CMP_RemotePlayer absent (expected when ESP unused)");
	}
}

void CMP_RegisterConsole()
{
	Steal("DumpTexturePalette", "cmp_join", "CommonwealthMP: join A.B.C.D Port (default 127.0.0.1:7777)", ExecJoin, g_joinParams, 5);
	Steal("DumpNiUpdates", "cmp_status", "CommonwealthMP: print join/ghost status and HUD pointer", ExecStatus);
	Steal("DumpModelMap", "cmp_leave", "CommonwealthMP: leave dedicated server", ExecLeave);
	Steal("ToggleMaterialGeometry", "cmp_lobby", "CommonwealthMP: probe Steam CreateLobby (discovery only)", ExecLobby);
	Steal("DumpConditionFunctions", "cmp_goto", "CommonwealthMP: warp to the fake/remote player", ExecGoto);
	Steal("DumpPapyrusStacks", "cmp_probeforms", "CommonwealthMP: dump live TESForm types for ghost spawn", ExecProbe);
	Steal("ToggleCollisionGeometry", "cmp_dump", "CommonwealthMP: write FormID/RVA/idle/cell dump for development", ExecDump);
	Steal("ToggleActorMover", "cmp_query", "CommonwealthMP: query if a live Commonwealth host is on A.B.C.D Port", ExecQuery, g_joinParams, 5);
}
