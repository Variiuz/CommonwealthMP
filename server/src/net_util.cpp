#include "server_state.hpp"

#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <sstream>

#include "log.hpp"

std::string addr_key(const sockaddr_in& a)
{
	char ip[INET_ADDRSTRLEN]{};
	inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
	return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

bool same_addr(const sockaddr_in& a, const sockaddr_in& b)
{
	return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

double now_sec()
{
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

void send_to(CmpSocket sock, const sockaddr_in& dest, const void* data, int len, const char* what)
{
	const int n = sendto(sock, static_cast<const char*>(data), len, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
	if (n != len) LOG_WARN("send %s failed bytes=%d/%d err=%d dest=%s", what, n, len, cmp::udp_last_error(), addr_key(dest).c_str());
}

void send_blob(CmpSocket sock, const sockaddr_in& dest, cmp::Msg type, std::uint32_t peerId, const std::vector<std::uint8_t>& blob)
{
	std::vector<std::vector<std::uint8_t>> packets;
	if (!cmp::split_blob_chunks(type, peerId, blob, packets)) return;
	const auto what = std::string(cmp::msg_name(type));
	for (const auto& pkt : packets) send_to(sock, dest, pkt.data(), static_cast<int>(pkt.size()), what.c_str());
}

bool in_interest(const Client& a, const Client& b, float maxUu)
{
	return cmp::in_interest(a.lastPose, a.havePose, b.lastPose, b.havePose, maxUu);
}

bool take_blob(cmp::BlobAssembly& assembly, const char* buf, int n, std::vector<std::uint8_t>& out)
{
	return cmp::assemble_blob_chunk(assembly, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(buf), static_cast<std::size_t>(n)), out)
		== cmp::AssembleStatus::Complete;
}

std::string lower_copy(std::string s)
{
	for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::vector<std::string> split_words(const std::string& line)
{
	std::vector<std::string> out;
	std::istringstream iss(line);
	std::string word;
	while (iss >> word) out.push_back(word);
	return out;
}

std::uint32_t alloc_peer(std::uint32_t& nextPeer)
{
	return nextPeer++;
}

std::uint32_t build_session_flags(const ServerConfig& cfg)
{
	std::uint32_t flags = 0;
	if (cfg.pvp) flags |= cmp::kSessionPvpEnabled;
	if (!cfg.password.empty()) flags |= cmp::kSessionPasswordRequired;
	return flags;
}

void log_json_event(bool enabled, const std::string& json)
{
	if (enabled) LOG_INFO("%s", json.c_str());
}

Client* find_host(std::unordered_map<std::uint32_t, Client>& clients, SessionWorld& world)
{
	for (auto& [_, client] : clients) {
		if (client.peerId == world.hostPeerId) return &client;
	}
	if (clients.empty()) {
		world.hostPeerId = 0;
		return nullptr;
	}
	world.hostPeerId = clients.begin()->second.peerId;
	LOG_INFO("host is now peer %u", world.hostPeerId);
	return &clients.begin()->second;
}

#ifdef _WIN32
namespace {
ULARGE_INTEGER ft_to_ularge(const FILETIME& ft)
{
	ULARGE_INTEGER value{};
	value.LowPart = ft.dwLowDateTime;
	value.HighPart = ft.dwHighDateTime;
	return value;
}
}

void sample_proc_stats(ProcStats& stats)
{
	FILETIME ignoreCreate{}, ignoreExit{}, procKernel{}, procUser{}, sysIdle{}, sysKernel{}, sysUser{};
	if (!GetProcessTimes(GetCurrentProcess(), &ignoreCreate, &ignoreExit, &procKernel, &procUser)
		|| !GetSystemTimes(&sysIdle, &sysKernel, &sysUser)) return;
	const auto proc = ft_to_ularge(procKernel).QuadPart + ft_to_ularge(procUser).QuadPart;
	const auto sys = ft_to_ularge(sysKernel).QuadPart + ft_to_ularge(sysUser).QuadPart;
	if (stats.haveSample && sys > stats.lastSys.QuadPart) {
		stats.cpuPercent = 100.0 * static_cast<double>(proc - stats.lastProc.QuadPart) / static_cast<double>(sys - stats.lastSys.QuadPart);
		if (stats.cpuPercent < 0.0) stats.cpuPercent = 0.0;
		if (stats.cpuPercent > 100.0) stats.cpuPercent = 100.0;
	}
	stats.lastProc.QuadPart = proc;
	stats.lastSys.QuadPart = sys;
	stats.haveSample = true;
	using K32GetProcessMemoryInfo_t = BOOL(WINAPI*)(HANDLE, void*, DWORD);
	struct PROCESS_MEMORY_COUNTERS_LITE {
		DWORD cb, pageFaultCount;
		SIZE_T peakWorkingSetSize, workingSetSize, quotaPeakPagedPoolUsage, quotaPagedPoolUsage, quotaPeakNonPagedPoolUsage, quotaNonPagedPoolUsage, pagefileUsage, peakPagefileUsage;
	};
	static const auto getMem = reinterpret_cast<K32GetProcessMemoryInfo_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
	if (getMem) {
		PROCESS_MEMORY_COUNTERS_LITE pmc{};
		pmc.cb = sizeof(pmc);
		if (getMem(GetCurrentProcess(), &pmc, sizeof(pmc))) stats.memMb = static_cast<double>(pmc.workingSetSize) / (1024.0 * 1024.0);
	}
}
#else
void sample_proc_stats(ProcStats&) {}
#endif

void print_usage()
{
	LOG_INFO("CommonwealthMP.Server [options]");
	LOG_INFO("  --config PATH       Config file (default exe_dir/server.ini)");
	LOG_INFO("  --name TEXT         Server display name");
	LOG_INFO("  --motd TEXT         Message of the day (SessionInfo)");
	LOG_INFO("  --port N            TCP+UDP port (default 7777)");
	LOG_INFO("  --max-players N     Cap live clients (default 8)");
	LOG_INFO("  --interest-uu N     Pose fan-out radius (0=off, default 20000)");
	LOG_INFO("  --reset-session     Clear session/ world and player records");
	LOG_INFO("  --log-file PATH     Log file (default next to the exe)");
	LOG_INFO("  --session-dir PATH  Session folder (default next to the exe / session)");
	LOG_INFO("  --verbose           DEBUG on console as well as the log file");
	LOG_INFO("  --quiet             WARN+ on console only");
	LOG_INFO("  --json-log          Extra JSON lines for join/leave/kick/full");
	LOG_INFO("  --pvp               Enable PvP hit relay (default)");
	LOG_INFO("  --no-pvp            Disable PvP hit relay");
	LOG_INFO("  --password TEXT     Require password on Hello (max 15 chars)");
	LOG_INFO("  --mod-hash HEX      Pin expected mod fingerprint (optional)");
	LOG_INFO("  --ban KEY           Reject Hello from player key at startup");
	LOG_INFO("Admin: help status players kick <peer|key> [ban] say TEXT save quit maxplayers N reload motd TEXT pvp on|off password TEXT ban|unban|bans");
}

void print_banner()
{
	ServerLog::instance().write_banner("\n"
		"   ____                                      _ _   _      __  __ ____  \n"
		"  / ___|___  _ __ ___  _ __ ___   ___  _ __ | | | | |    |  \\/  |  _ \\ \n"
		" | |   / _ \\| '_ ` _ \\| '_ ` _ \\ / _ \\| '_ \\| | |_| |____| |\\/| | |_) |\n"
		" | |__| (_) | | | | | | | | | | | (_) | | | | |  _  |____| |  | |  __/ \n"
		"  \\____\\___/|_| |_| |_| |_| |_|\\___/|_| |_|_| |_|    |_|_|  |_|_|    \n"
		"\n");
}
