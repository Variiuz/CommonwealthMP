#include "pch.h"
#include "cmp.h"
#include "cmp_util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

std::string IniPath()
{
	char path[REX::W32::MAX_PATH]{};
	REX::W32::GetModuleFileNameA(REX::W32::GetCurrentModule(), path, REX::W32::MAX_PATH);
	std::string full = path;
	const auto dot = full.find_last_of('.');
	if (dot != std::string::npos) {
		full = full.substr(0, dot) + ".ini";
	} else {
		full += ".ini";
	}
	return full;
}

std::string KeyPath(const std::string& ini)
{
	const auto dot = ini.find_last_of('.');
	return (dot == std::string::npos ? ini : ini.substr(0, dot)) + ".playerkey";
}

std::string IniStr(const char* section, const char* key, const char* fallback, const std::string& ini)
{
	char buf[512]{};
	REX::W32::GetPrivateProfileStringA(section, key, fallback, buf, static_cast<std::uint32_t>(sizeof(buf)), ini.c_str());
	return buf;
}

std::string SanitizeKey(std::string raw)
{
	return cmp::sanitize_player_key(raw, {});
}

std::string GenerateKey()
{
	char path[REX::W32::MAX_PATH]{};
	REX::W32::GetModuleFileNameA(REX::W32::GetCurrentModule(), path, REX::W32::MAX_PATH);
	std::uint32_t h = 2166136261u;
	for (const char* p = path; *p; ++p) {
		h ^= static_cast<unsigned char>(*p);
		h *= 16777619u;
	}
	const auto t = static_cast<std::uint32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);
	char buf[17]{};
	std::snprintf(buf, sizeof(buf), "%08X%08X", h, t);
	return buf;
}

std::string LoadOrCreatePlayerKey(const std::string& ini)
{
	auto key = SanitizeKey(IniStr("Session", "PlayerKey", "", ini));
	if (!key.empty()) {
		return key;
	}
	const auto side = KeyPath(ini);
	if (std::ifstream in(side); in) {
		std::string line;
		std::getline(in, line);
		key = SanitizeKey(line);
		if (!key.empty()) {
			return key;
		}
	}
	key = GenerateKey();
	std::ofstream out(side, std::ios::trunc);
	out << key;
	REX::INFO("Generated playerKey {} ({})", key, side);
	return key;
}

}  // namespace

Session& CMP_Session()
{
	static Session s;
	return s;
}

void CMP_LoadSettings()
{
	auto& s = CMP_Session();
	const auto ini = IniPath();
	s.settings.host = IniStr("Network", "Host", "127.0.0.1", ini);
	s.settings.port = static_cast<std::uint16_t>(
		REX::W32::GetPrivateProfileIntA("Network", "Port", cmp::kDefaultPort, ini.c_str()));
	s.settings.autoJoin = REX::W32::GetPrivateProfileIntA("Network", "AutoJoin", 0, ini.c_str()) != 0;
	s.settings.ghostEditorId = IniStr("Ghost", "EditorID", "CMP_RemotePlayer", ini);
	s.settings.ghostSpawn = REX::W32::GetPrivateProfileIntA("Ghost", "Spawn", 1, ini.c_str()) != 0;
	{
		const auto raw = IniStr("Ghost", "SourceForm", "1D323", ini);
		char* end = nullptr;
		const auto parsed = std::strtoul(raw.c_str(), &end, 16);
		s.settings.ghostSourceForm = (end && end != raw.c_str() && parsed != 0)
			? static_cast<std::uint32_t>(parsed)
			: 0x0001D323u;
	}
	s.settings.poseHz = std::max(1, static_cast<int>(REX::W32::GetPrivateProfileIntA("Debug", "PoseHz", 20, ini.c_str())));
	s.settings.pointerHud = REX::W32::GetPrivateProfileIntA("Debug", "PointerHud", 1, ini.c_str()) != 0;
	s.settings.pointerSeconds = std::max(1, static_cast<int>(REX::W32::GetPrivateProfileIntA("Debug", "PointerSeconds", 4, ini.c_str())));
	const auto iniName = IniStr("Session", "PlayerName", "", ini);
	const char* nameSrc = "ini";
	if (!iniName.empty()) {
		s.settings.playerName = iniName;
	} else {
		auto steam = CMP_SteamPersonaName();
		if (!steam.empty()) {
			s.settings.playerName = std::move(steam);
			nameSrc = "steam";
		} else {
			s.settings.playerName = "fo4";
			nameSrc = "fallback";
		}
	}
	s.settings.playerKey = LoadOrCreatePlayerKey(ini);
	REX::INFO("INI {} Host={}:{} AutoJoin={} ghost={} spawn={} source={:08X} playerKey={} name={} nameSrc={} pointerHud={}",
		ini, s.settings.host, s.settings.port, s.settings.autoJoin, s.settings.ghostEditorId,
		s.settings.ghostSpawn, s.settings.ghostSourceForm, s.settings.playerKey, s.settings.playerName, nameSrc,
		s.settings.pointerHud);
}

void CMP_SaveNetworkSettings(const std::string& host, std::uint16_t port)
{
	auto& s = CMP_Session();
	s.settings.host = host.empty() ? "127.0.0.1" : host;
	s.settings.port = port == 0 ? cmp::kDefaultPort : port;
	const auto ini = IniPath();
	using WriteFn = std::int32_t(__stdcall*)(const char*, const char*, const char*, const char*);
	static const auto writeIni = reinterpret_cast<WriteFn>(
		REX::W32::GetProcAddress(REX::W32::GetModuleHandleA("kernel32.dll"), "WritePrivateProfileStringA"));
	if (writeIni) {
		writeIni("Network", "Host", s.settings.host.c_str(), ini.c_str());
		writeIni("Network", "Port", std::to_string(s.settings.port).c_str(), ini.c_str());
	} else {
		REX::WARN("WritePrivateProfileStringA missing; Host/Port kept in memory only");
	}
	REX::INFO("saved Network Host={}:{} -> {}", s.settings.host, s.settings.port, ini);
}
