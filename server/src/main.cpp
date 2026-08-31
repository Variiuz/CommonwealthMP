#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "admin.hpp"
#include "cmp_blobs.hpp"
#include "cmp_json.hpp"
#include "cmp_protocol.hpp"
#include "cmp_util.hpp"
#include "config.hpp"
#include "log.hpp"
#include "sim.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr int kMaxDatagram = 512;
constexpr double kClientTimeoutSec = 8.0;
constexpr int kFakeHz = 20;
constexpr double kPersistIntervalSec = 2.0;
constexpr double kStatusBarIntervalSec = 1.0;
constexpr double kStatusLogIntervalSec = 30.0;
constexpr const char* kServerVersion = "0.5.7";

namespace fs = std::filesystem;

std::atomic<bool> g_running{ true };

struct PlayerRec {
	std::string key;
	std::string name;
	bool havePose = false;
	std::uint32_t locationFormId = cmp::kCommonwealthWorldspace;
	float x = cmp::kSanctuaryX;
	float y = cmp::kSanctuaryY;
	float z = cmp::kSanctuaryZ;
	float yaw = 0.0f;
	std::vector<std::uint8_t> appearance;
	std::vector<std::uint8_t> inventory;
};

struct Client {
	sockaddr_in addr{};
	std::uint32_t peerId = 0;
	std::string playerKey;
	std::string name;
	double lastSeen = 0.0;
	std::uint64_t posesIn = 0;
	cmp::PlayerPose lastPose{};
	bool havePose = false;
	std::vector<std::uint8_t> appearance;
	std::vector<std::uint8_t> inventory;
};

struct SessionWorld {
	bool created = false;
	std::uint32_t spawnLocation = cmp::kCommonwealthWorldspace;
	float spawnX = cmp::kSanctuaryX;
	float spawnY = cmp::kSanctuaryY;
	float spawnZ = cmp::kSanctuaryZ;
	float gameHour = 10.0f;
	float gameDaysPassed = 0.0f;
	std::uint32_t weatherFormId = 0;
	std::uint32_t hostPeerId = 0;
	std::uint32_t hostLocation = cmp::kCommonwealthWorldspace;
	float hostX = cmp::kSanctuaryX;
	float hostY = cmp::kSanctuaryY;
	float hostZ = cmp::kSanctuaryZ;
};

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

std::string exe_dir()
{
	char path[MAX_PATH]{};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string full = path;
	const auto slash = full.find_last_of("\\/");
	return slash == std::string::npos ? "." : full.substr(0, slash);
}

fs::path g_sessionDir;

fs::path session_dir()
{
	if (!g_sessionDir.empty()) {
		return g_sessionDir;
	}
	return fs::path(exe_dir()) / "session";
}

void send_to(SOCKET sock, const sockaddr_in& dest, const void* data, int len, const char* what)
{
	const int n = sendto(sock, static_cast<const char*>(data), len, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
	if (n != len) {
		LOG_WARN("send %s failed bytes=%d/%d wsa=%d dest=%s", what, n, len, WSAGetLastError(), addr_key(dest).c_str());
	}
}

void send_blob(SOCKET sock, const sockaddr_in& dest, cmp::Msg type, std::uint32_t peerId, const std::vector<std::uint8_t>& blob)
{
	std::vector<std::vector<std::uint8_t>> packets;
	if (!cmp::split_blob_chunks(type, peerId, blob, packets)) {
		return;
	}
	const auto what = std::string(cmp::msg_name(type));
	for (const auto& pkt : packets) {
		send_to(sock, dest, pkt.data(), static_cast<int>(pkt.size()), what.c_str());
	}
}

std::string json_quoted(const std::string& text, const char* key)
{
	return cmp::json_quoted(text, key);
}

double json_number(const std::string& text, const char* key, double fallback)
{
	return cmp::json_number(text, key, fallback);
}

void persist_world(const SessionWorld& world)
{
	std::error_code ec;
	fs::create_directories(session_dir() / "players", ec);
	std::ofstream json(session_dir() / "world.json", std::ios::trunc);
	if (!json) {
		return;
	}
	json << "{\n"
		 << "  \"pluginVersion\": " << cmp::kPluginVersion << ",\n"
		 << "  \"spawnLocation\": " << world.spawnLocation << ",\n"
		 << "  \"spawnX\": " << world.spawnX << ",\n"
		 << "  \"spawnY\": " << world.spawnY << ",\n"
		 << "  \"spawnZ\": " << world.spawnZ << ",\n"
		 << "  \"gameHour\": " << world.gameHour << ",\n"
		 << "  \"gameDaysPassed\": " << world.gameDaysPassed << ",\n"
		 << "  \"weatherFormId\": " << world.weatherFormId << "\n"
		 << "}\n";
}

void persist_player(const PlayerRec& rec)
{
	if (rec.key.empty()) {
		return;
	}
	std::error_code ec;
	const auto dir = session_dir() / "players";
	fs::create_directories(dir, ec);
	std::ofstream json(dir / (rec.key + ".json"), std::ios::trunc);
	if (json) {
		json << "{\n"
			 << "  \"key\": \"" << rec.key << "\",\n"
			 << "  \"name\": \"" << rec.name << "\",\n"
			 << "  \"havePose\": " << (rec.havePose ? 1 : 0) << ",\n"
			 << "  \"locationFormId\": " << rec.locationFormId << ",\n"
			 << "  \"x\": " << rec.x << ",\n"
			 << "  \"y\": " << rec.y << ",\n"
			 << "  \"z\": " << rec.z << ",\n"
			 << "  \"yaw\": " << rec.yaw << "\n"
			 << "}\n";
	}
	if (!rec.appearance.empty()) {
		std::ofstream bin(dir / (rec.key + ".appearance.bin"), std::ios::binary | std::ios::trunc);
		bin.write(reinterpret_cast<const char*>(rec.appearance.data()), static_cast<std::streamsize>(rec.appearance.size()));
	}
	if (!rec.inventory.empty()) {
		std::ofstream bin(dir / (rec.key + ".inventory.bin"), std::ios::binary | std::ios::trunc);
		bin.write(reinterpret_cast<const char*>(rec.inventory.data()), static_cast<std::streamsize>(rec.inventory.size()));
	}
}

void flush_dirty(
	SessionWorld& world,
	bool& worldDirty,
	std::unordered_map<std::string, PlayerRec>& players,
	std::unordered_set<std::string>& dirtyPlayers)
{
	if (worldDirty) {
		persist_world(world);
		worldDirty = false;
	}
	for (const auto& key : dirtyPlayers) {
		if (auto it = players.find(key); it != players.end()) {
			persist_player(it->second);
		}
	}
	dirtyPlayers.clear();
}

std::vector<std::uint8_t> read_bin(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in) {
		return {};
	}
	const auto pos = in.tellg();
	if (pos < 0 || pos > static_cast<std::streamoff>(1 << 20)) {
		return {};
	}
	const auto n = static_cast<std::size_t>(pos);
	in.seekg(0);
	std::vector<std::uint8_t> out(n);
	in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
	return out;
}

void load_world(SessionWorld& world, std::unordered_map<std::string, PlayerRec>& players)
{
	const auto wpath = session_dir() / "world.json";
	std::ifstream in(wpath);
	if (in) {
		std::stringstream ss;
		ss << in.rdbuf();
		const auto text = ss.str();
		world.created = true;
		world.spawnLocation = static_cast<std::uint32_t>(json_number(text, "spawnLocation", world.spawnLocation));
		world.spawnX = static_cast<float>(json_number(text, "spawnX", world.spawnX));
		world.spawnY = static_cast<float>(json_number(text, "spawnY", world.spawnY));
		world.spawnZ = static_cast<float>(json_number(text, "spawnZ", world.spawnZ));
		world.gameHour = static_cast<float>(json_number(text, "gameHour", world.gameHour));
		world.gameDaysPassed = static_cast<float>(json_number(text, "gameDaysPassed", world.gameDaysPassed));
		world.weatherFormId = static_cast<std::uint32_t>(json_number(text, "weatherFormId", 0));
		LOG_INFO("world.json loaded spawn=(%.0f,%.0f,%.0f)", world.spawnX, world.spawnY, world.spawnZ);
	}

	std::error_code ec;
	const auto dir = session_dir() / "players";
	if (!fs::exists(dir, ec)) {
		return;
	}
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".json") {
			continue;
		}
		std::ifstream pf(entry.path());
		if (!pf) {
			continue;
		}
		std::stringstream ss;
		ss << pf.rdbuf();
		const auto text = ss.str();
		PlayerRec rec;
		rec.key = json_quoted(text, "key");
		if (rec.key.empty()) {
			rec.key = entry.path().stem().string();
		}
		rec.name = json_quoted(text, "name");
		rec.havePose = json_number(text, "havePose", 0) != 0;
		rec.locationFormId = static_cast<std::uint32_t>(json_number(text, "locationFormId", rec.locationFormId));
		rec.x = static_cast<float>(json_number(text, "x", rec.x));
		rec.y = static_cast<float>(json_number(text, "y", rec.y));
		rec.z = static_cast<float>(json_number(text, "z", rec.z));
		rec.yaw = static_cast<float>(json_number(text, "yaw", 0));
		rec.appearance = read_bin(dir / (rec.key + ".appearance.bin"));
		rec.inventory = read_bin(dir / (rec.key + ".inventory.bin"));
		players[rec.key] = rec;
		LOG_INFO("player %s loaded pose=%d appear=%zu inv=%zu",
			rec.key.c_str(), rec.havePose ? 1 : 0, rec.appearance.size(), rec.inventory.size());
	}
}

void reject_to(SOCKET sock, const sockaddr_in& dest, cmp::RejectReason reason, const char* text)
{
	const auto msg = cmp::make_reject(reason, text);
	send_to(sock, dest, &msg, sizeof(msg), "Reject");
	LOG_INFO("tx Reject %s (%s) -> %s", cmp::reject_name(reason), text, addr_key(dest).c_str());
}

BOOL WINAPI on_ctrl(DWORD type)
{
	LOG_INFO("console signal %lu, shutting down", static_cast<unsigned long>(type));
	g_running = false;
	return TRUE;
}

LONG WINAPI on_crash(EXCEPTION_POINTERS*)
{
	LOG_ERROR("unhandled exception, writing session and exiting");
	g_running = false;
	return EXCEPTION_CONTINUE_SEARCH;
}

void print_usage()
{
	LOG_INFO("CommonwealthMP.Server.exe [options]");
	LOG_INFO("  --config PATH       Config file (default exe_dir/server.ini)");
	LOG_INFO("  --name TEXT         Server display name");
	LOG_INFO("  --motd TEXT         Message of the day (SessionInfo)");
	LOG_INFO("  --port N            UDP port (default 7777)");
	LOG_INFO("  --max-players N     Cap live clients (default 8)");
	LOG_INFO("  --interest-uu N     Pose fan-out radius (0=off, default 20000)");
	LOG_INFO("  --fake              Invent a second player until 2 real clients (default)");
	LOG_INFO("  --no-fake           Do not invent a second player");
	LOG_INFO("  --reset-session     Clear session/ world and player records");
	LOG_INFO("  --log-file PATH     Log file (default next to the exe)");
	LOG_INFO("  --session-dir PATH  Session folder (default next to the exe / session)");
	LOG_INFO("  --verbose           DEBUG on console as well as the log file");
	LOG_INFO("  --quiet             WARN+ on console only");
	LOG_INFO("  --json-log          Extra JSON lines for join/leave/kick/full");
	LOG_INFO("Admin: help status players kick save quit fake on|off maxplayers N reload motd TEXT");
}

void print_banner()
{
	ServerLog::instance().write_banner(
		"\n"
		"   ____                                      _ _   _      __  __ ____  \n"
		"  / ___|___  _ __ ___  _ __ ___   ___  _ __ | | | | |    |  \\/  |  _ \\ \n"
		" | |   / _ \\| '_ ` _ \\| '_ ` _ \\ / _ \\| '_ \\| | |_| |____| |\\/| | |_) |\n"
		" | |__| (_) | | | | | | | | | | | (_) | | | | |  _  |____| |  | |  __/ \n"
		"  \\____\\___/|_| |_| |_|_| |_| |_|\\___/|_| |_|_|_| |_|    |_|  |_|_|    \n"
		"\n");
}

void backup_session_folder()
{
	const auto src = session_dir();
	std::error_code ec;
	if (!fs::exists(src, ec)) {
		return;
	}
	const auto dst = src.parent_path() / (src.filename().string() + ".bak");
	fs::remove_all(dst, ec);
	fs::copy(src, dst, fs::copy_options::recursive, ec);
	if (ec) {
		LOG_WARN("session backup failed: %s", ec.message().c_str());
	} else {
		LOG_INFO("session backed up to %s", dst.string().c_str());
	}
}

void log_json_event(bool enabled, const std::string& json)
{
	if (enabled) {
		LOG_INFO("%s", json.c_str());
	}
}

bool in_interest(const Client& a, const Client& b, float maxUu)
{
	return cmp::in_interest(a.lastPose, a.havePose, b.lastPose, b.havePose, maxUu);
}

struct ProcStats {
	double cpuPercent = 0.0;
	double memMb = 0.0;
	ULARGE_INTEGER lastSys{};
	ULARGE_INTEGER lastProc{};
	bool haveSample = false;
};

ULARGE_INTEGER ft_to_ularge(const FILETIME& ft)
{
	ULARGE_INTEGER u{};
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return u;
}

void sample_proc_stats(ProcStats& s)
{
	FILETIME ignoreCreate{}, ignoreExit{}, procKernel{}, procUser{};
	FILETIME sysIdle{}, sysKernel{}, sysUser{};
	if (!GetProcessTimes(GetCurrentProcess(), &ignoreCreate, &ignoreExit, &procKernel, &procUser)
		|| !GetSystemTimes(&sysIdle, &sysKernel, &sysUser)) {
		return;
	}
	const auto proc = ft_to_ularge(procKernel).QuadPart + ft_to_ularge(procUser).QuadPart;
	const auto sys = ft_to_ularge(sysKernel).QuadPart + ft_to_ularge(sysUser).QuadPart;
	if (s.haveSample && sys > s.lastSys.QuadPart) {
		const auto dProc = proc - s.lastProc.QuadPart;
		const auto dSys = sys - s.lastSys.QuadPart;
		s.cpuPercent = 100.0 * static_cast<double>(dProc) / static_cast<double>(dSys);
		if (s.cpuPercent < 0.0) {
			s.cpuPercent = 0.0;
		}
		if (s.cpuPercent > 100.0) {
			s.cpuPercent = 100.0;
		}
	}
	s.lastProc.QuadPart = proc;
	s.lastSys.QuadPart = sys;
	s.haveSample = true;

	using K32GetProcessMemoryInfo_t = BOOL(WINAPI*)(HANDLE, void*, DWORD);
	struct PROCESS_MEMORY_COUNTERS_LITE {
		DWORD cb;
		DWORD pageFaultCount;
		SIZE_T peakWorkingSetSize;
		SIZE_T workingSetSize;
		SIZE_T quotaPeakPagedPoolUsage;
		SIZE_T quotaPagedPoolUsage;
		SIZE_T quotaPeakNonPagedPoolUsage;
		SIZE_T quotaNonPagedPoolUsage;
		SIZE_T pagefileUsage;
		SIZE_T peakPagefileUsage;
	};
	static const auto getMem = reinterpret_cast<K32GetProcessMemoryInfo_t>(
		GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
	if (getMem) {
		PROCESS_MEMORY_COUNTERS_LITE pmc{};
		pmc.cb = sizeof(pmc);
		if (getMem(GetCurrentProcess(), &pmc, sizeof(pmc))) {
			s.memMb = static_cast<double>(pmc.workingSetSize) / (1024.0 * 1024.0);
		}
	}
}

Client* find_host(std::unordered_map<std::string, Client>& clients, SessionWorld& world)
{
	for (auto& [_, c] : clients) {
		if (c.peerId == world.hostPeerId) {
			return &c;
		}
	}
	if (clients.empty()) {
		world.hostPeerId = 0;
		return nullptr;
	}
	world.hostPeerId = clients.begin()->second.peerId;
	LOG_INFO("host is now peer %u", world.hostPeerId);
	return &clients.begin()->second;
}

bool take_blob(cmp::BlobAssembly& a, const char* buf, int n, std::vector<std::uint8_t>& out)
{
	return cmp::assemble_blob_chunk(
			   a,
			   std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(buf), static_cast<std::size_t>(n)),
			   out)
		== cmp::AssembleStatus::Complete;
}

std::string lower_copy(std::string s)
{
	for (char& c : s) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

std::vector<std::string> split_words(const std::string& line)
{
	std::vector<std::string> out;
	std::istringstream iss(line);
	std::string w;
	while (iss >> w) {
		out.push_back(w);
	}
	return out;
}

}  // namespace

#ifdef _WIN32
// Windows 11 default terminal is often Windows Terminal. Always open a classic
// conhost window unless CMP_CONHOST=1 (tests / nested launch set this).
static bool relaunch_in_classic_console()
{
	char probe[64]{};
	if (GetEnvironmentVariableA("CMP_CONHOST", probe, static_cast<DWORD>(sizeof(probe))) > 0) {
		return false;
	}

	wchar_t sysDir[MAX_PATH]{};
	if (GetSystemDirectoryW(sysDir, MAX_PATH) == 0) {
		return false;
	}
	std::wstring conhost = sysDir;
	conhost += L"\\conhost.exe";

	std::wstring cmd = L"\"";
	cmd += conhost;
	cmd += L"\" ";
	cmd += GetCommandLineW();

	SetEnvironmentVariableA("CMP_CONHOST", "1");

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const BOOL ok = CreateProcessW(
		conhost.c_str(),
		cmd.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NEW_CONSOLE,
		nullptr,
		nullptr,
		&si,
		&pi);
	if (!ok) {
		SetEnvironmentVariableA("CMP_CONHOST", nullptr);
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
	if (relaunch_in_classic_console()) {
		return 0;
	}
#endif
	ServerLog::instance().ensure_console();
	SetConsoleCtrlHandler(on_ctrl, TRUE);
	SetUnhandledExceptionFilter(on_crash);

	ServerConfig cfg;
	cfg.logFile = exe_dir() + "\\CommonwealthMP.Server.log";
	std::string configPath = exe_dir() + "\\server.ini";

	// First pass: locate --config before loading ini
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--config" && i + 1 < argc) {
			configPath = argv[++i];
			cfg.configPath = configPath;
		}
	}
	bool createdIni = false;
	if (!ensure_server_ini(configPath, cfg, &createdIni)) {
		// Keep built-in defaults if the path is unwritable; CLI can still override.
	}
	if (cfg.logFile.empty()) {
		cfg.logFile = exe_dir() + "\\CommonwealthMP.Server.log";
	}

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--help" || arg == "-h") {
			print_usage();
			return 0;
		}
		if (arg == "--config" && i + 1 < argc) {
			++i;
			continue;
		}
		if (arg == "--fake") {
			cfg.fake = true;
			continue;
		}
		if (arg == "--no-fake") {
			cfg.fake = false;
			continue;
		}
		if (arg == "--reset-session") {
			cfg.resetSession = true;
			continue;
		}
		if (arg == "--verbose") {
			cfg.verbose = true;
			cfg.quiet = false;
			continue;
		}
		if (arg == "--quiet") {
			cfg.quiet = true;
			cfg.verbose = false;
			continue;
		}
		if (arg == "--port" && i + 1 < argc) {
			cfg.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
			continue;
		}
		if (arg == "--max-players" && i + 1 < argc) {
			cfg.maxPlayers = std::stoi(argv[++i]);
			if (cfg.maxPlayers < 1) {
				cfg.maxPlayers = 1;
			}
			continue;
		}
		if (arg == "--log-file" && i + 1 < argc) {
			cfg.logFile = argv[++i];
			continue;
		}
		if (arg == "--session-dir" && i + 1 < argc) {
			cfg.sessionDir = argv[++i];
			continue;
		}
		if (arg == "--name" && i + 1 < argc) {
			cfg.name = argv[++i];
			continue;
		}
		if (arg == "--motd" && i + 1 < argc) {
			cfg.motd = argv[++i];
			continue;
		}
		if (arg == "--interest-uu" && i + 1 < argc) {
			cfg.interestUu = std::stof(argv[++i]);
			if (cfg.interestUu < 0.0f) {
				cfg.interestUu = 0.0f;
			}
			continue;
		}
		if (arg == "--json-log") {
			cfg.jsonLog = true;
			continue;
		}
		LOG_ERROR("Unknown arg: %s", arg.c_str());
		print_usage();
		return 2;
	}

	const LogLevel consoleLevel = cfg.verbose ? LogLevel::Debug : (cfg.quiet ? LogLevel::Warn : LogLevel::Info);
	ServerLog::instance().set_level(consoleLevel, LogLevel::Debug);
	if (!ServerLog::instance().open_file(cfg.logFile)) {
		LOG_ERROR("Could not open log file %s", cfg.logFile.c_str());
	}

	ServerLog::instance().set_title("CMP " + cfg.name);

	if (!cfg.sessionDir.empty()) {
		g_sessionDir = cfg.sessionDir;
	}

	print_banner();
	LOG_INFO("CommonwealthMP.Server %s starting", kServerVersion);
	if (createdIni) {
		LOG_INFO("wrote default config %s", configPath.c_str());
	}
	LOG_INFO("config=%s", configPath.c_str());
	LOG_INFO("name=%s port=%u max_players=%d fake=%s interest_uu=%.0f",
		cfg.name.c_str(),
		static_cast<unsigned>(cfg.port),
		cfg.maxPlayers,
		cfg.fake ? "on" : "off",
		cfg.interestUu);
	if (!cfg.motd.empty()) {
		LOG_INFO("motd=%s", cfg.motd.c_str());
	}
	LOG_INFO("session=%s", session_dir().string().c_str());
	LOG_INFO("log file: %s (DEBUG in file, %s on console%s)",
		ServerLog::instance().path().c_str(),
		cfg.verbose ? "DEBUG" : (cfg.quiet ? "WARN+" : "INFO"),
		cfg.jsonLog ? ", json events" : "");
	LOG_DEBUG("protocol magic=CMP1 version=%u plugin=%u defaultPort=%u config=%s",
		static_cast<unsigned>(cmp::kProtocolVersion),
		cmp::kPluginVersion,
		static_cast<unsigned>(cmp::kDefaultPort),
		configPath.c_str());

	if (cfg.resetSession) {
		std::error_code ec;
		fs::remove_all(session_dir(), ec);
		LOG_INFO("session folder cleared");
	} else {
		backup_session_folder();
	}

	SessionWorld world;
	std::unordered_map<std::string, PlayerRec> players;
	load_world(world, players);

	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		LOG_ERROR("WSAStartup failed wsa=%d", WSAGetLastError());
		return 1;
	}

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) {
		LOG_ERROR("socket failed wsa=%d", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	BOOL reuse = TRUE;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

	sockaddr_in bindAddr{};
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	bindAddr.sin_port = htons(cfg.port);
	if (bind(sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
		LOG_ERROR("bind failed on UDP %u wsa=%d", static_cast<unsigned>(cfg.port), WSAGetLastError());
		closesocket(sock);
		WSACleanup();
		return 1;
	}

	u_long nonblock = 1;
	ioctlsocket(sock, FIONBIO, &nonblock);

	LOG_INFO("listening UDP 0.0.0.0:%u", static_cast<unsigned>(cfg.port));
	LOG_INFO("fake peer: %s (off at 2 clients)", cfg.fake ? "on" : "off");
	LOG_INFO("Join from FO4: load any save, cmp_join 127 0 0 1 %u (guests meet host exterior). Menu join requires a live host.",
		static_cast<unsigned>(cfg.port));
	LOG_INFO("Type 'help' for admin commands");

	std::unordered_map<std::string, Client> clients;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> appearAsm;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> invAsm;
	std::unordered_set<std::string> dirtyPlayers;
	bool worldDirty = false;
	std::uint32_t nextPeer = 1;
	auto lastFake = now_sec();
	auto lastPersist = now_sec();
	auto lastStatusBar = now_sec();
	auto lastStatusLog = now_sec();
	double fakeAngle = 0.0;
	int poseLog = 0;
	std::uint64_t datagrams = 0;
	std::uint64_t badHeaders = 0;
	bool fakeWasOn = false;
	bool fakeWanted = cfg.fake;
	ProcStats procStats{};
	sample_proc_stats(procStats);
	std::unordered_map<std::string, cmp::RateBucket> queryRates;
	std::unordered_map<std::string, cmp::RateBucket> helloRates;

	if (!cfg.quiet) {
		ServerLog::instance().set_status_enabled(true);
		char bar[256]{};
		std::snprintf(
			bar,
			sizeof(bar),
			"CMP %s :%u | %zu/%d | host %u | fake %s | rx %llu | cpu %.0f%% | mem %.0fMB",
			kServerVersion,
			static_cast<unsigned>(cfg.port),
			static_cast<std::size_t>(0),
			cfg.maxPlayers,
			world.hostPeerId,
			fakeWanted ? "on" : "off",
			static_cast<unsigned long long>(0),
			procStats.cpuPercent,
			procStats.memMb);
		ServerLog::instance().set_status(bar);
	}

	AdminConsole admin;
	admin.start();

	while (g_running) {
		const double t = now_sec();
		const bool fakeActive = fakeWanted && clients.size() < 2;

		std::string cmdLine;
		while (admin.poll(cmdLine)) {
			const auto words = split_words(cmdLine);
			if (words.empty()) {
				continue;
			}
			const auto cmd = lower_copy(words[0]);
			if (cmd == "help" || cmd == "?") {
				LOG_INFO("commands: help status players kick <peer|key> save quit fake on|off maxplayers N reload motd TEXT");
			} else if (cmd == "status") {
				LOG_INFO("status name=%s port=%u clients=%zu/%d host=%u fake=%s interest=%.0f datagrams=%llu bad=%llu",
					cfg.name.c_str(),
					static_cast<unsigned>(cfg.port),
					clients.size(),
					cfg.maxPlayers,
					world.hostPeerId,
					fakeWanted ? "on" : "off",
					cfg.interestUu,
					static_cast<unsigned long long>(datagrams),
					static_cast<unsigned long long>(badHeaders));
			} else if (cmd == "players") {
				if (clients.empty()) {
					LOG_INFO("players: (none)");
				}
				for (const auto& [ak, c] : clients) {
					LOG_INFO("player peer=%u key=%s name=%s addr=%s poses=%llu last=%.1fs",
						c.peerId,
						c.playerKey.c_str(),
						c.name.c_str(),
						ak.c_str(),
						static_cast<unsigned long long>(c.posesIn),
						t - c.lastSeen);
				}
			} else if (cmd == "kick" && words.size() >= 2) {
				const auto& target = words[1];
				bool kicked = false;
				for (auto it = clients.begin(); it != clients.end();) {
					const bool matchPeer = std::to_string(it->second.peerId) == target;
					const bool matchKey = it->second.playerKey == target;
					if (!matchPeer && !matchKey) {
						++it;
						continue;
					}
					LOG_INFO("kick peer=%u key=%s", it->second.peerId, it->second.playerKey.c_str());
					log_json_event(cfg.jsonLog,
						std::string("{\"event\":\"kick\",\"peer\":") + std::to_string(it->second.peerId)
							+ ",\"key\":\"" + it->second.playerKey + "\"}");
					const auto bye = cmp::make_bye(it->second.peerId);
					send_to(sock, it->second.addr, &bye, sizeof(bye), "ByeKick");
					if (auto pit = players.find(it->second.playerKey); pit != players.end()) {
						dirtyPlayers.insert(pit->first);
					}
					if (it->second.peerId == world.hostPeerId) {
						world.hostPeerId = 0;
					}
					it = clients.erase(it);
					find_host(clients, world);
					kicked = true;
					break;
				}
				if (!kicked) {
					LOG_WARN("kick: no client matching %s", target.c_str());
				}
			} else if (cmd == "save") {
				for (const auto& [_, c] : clients) {
					dirtyPlayers.insert(c.playerKey);
				}
				worldDirty = true;
				flush_dirty(world, worldDirty, players, dirtyPlayers);
				LOG_INFO("session saved");
			} else if (cmd == "quit" || cmd == "exit") {
				LOG_INFO("quit requested");
				g_running = false;
			} else if (cmd == "fake" && words.size() >= 2) {
				const auto arg = lower_copy(words[1]);
				if (arg == "on") {
					fakeWanted = true;
					LOG_INFO("fake peer: on");
				} else if (arg == "off") {
					fakeWanted = false;
					if (fakeWasOn) {
						const auto byeFake = cmp::make_bye(cmp::kFakePeerId);
						for (auto& [_, client] : clients) {
							send_to(sock, client.addr, &byeFake, sizeof(byeFake), "ByeFake");
						}
						fakeWasOn = false;
					}
					LOG_INFO("fake peer: off");
				} else {
					LOG_WARN("usage: fake on|off");
				}
			} else if (cmd == "maxplayers" && words.size() >= 2) {
				try {
					cfg.maxPlayers = std::max(1, std::stoi(words[1]));
					LOG_INFO("max_players=%d", cfg.maxPlayers);
				} catch (...) {
					LOG_WARN("usage: maxplayers N");
				}
			} else if (cmd == "reload") {
				ServerConfig reloaded = cfg;
				if (load_server_ini(configPath, reloaded)) {
					cfg.name = reloaded.name;
					cfg.motd = reloaded.motd;
					cfg.maxPlayers = reloaded.maxPlayers;
					cfg.fake = reloaded.fake;
					cfg.interestUu = reloaded.interestUu;
					cfg.jsonLog = reloaded.jsonLog;
					fakeWanted = cfg.fake;
					ServerLog::instance().set_title("CMP " + cfg.name);
					LOG_INFO("reloaded %s name=%s max=%d motd=%s",
						configPath.c_str(), cfg.name.c_str(), cfg.maxPlayers, cfg.motd.c_str());
				} else {
					LOG_WARN("reload failed: could not read %s", configPath.c_str());
				}
			} else if (cmd == "motd") {
				if (words.size() < 2) {
					LOG_INFO("motd=%s", cfg.motd.c_str());
				} else {
					cfg.motd.clear();
					for (std::size_t i = 1; i < words.size(); ++i) {
						if (i > 1) {
							cfg.motd.push_back(' ');
						}
						cfg.motd += words[i];
					}
					LOG_INFO("motd set to %s", cfg.motd.c_str());
				}
			} else {
				LOG_WARN("unknown command: %s (type help)", words[0].c_str());
			}
		}

		for (;;) {
			char buf[kMaxDatagram]{};
			sockaddr_in from{};
			int fromLen = sizeof(from);
			const int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
			if (n == SOCKET_ERROR) {
				const int err = WSAGetLastError();
				if (err != WSAEWOULDBLOCK && err != WSAETIMEDOUT) {
					LOG_WARN("recvfrom wsa=%d", err);
				}
				break;
			}
			if (n == 0) {
				break;
			}

			++datagrams;
			const auto key = addr_key(from);
			if (n < static_cast<int>(sizeof(cmp::Header))) {
				++badHeaders;
				break;
			}

			cmp::Header header{};
			std::memcpy(&header, buf, sizeof(header));
			if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
				++badHeaders;
				LOG_DEBUG("bad header from %s type=%u ver=%u size=%u nbytes=%d",
					key.c_str(),
					static_cast<unsigned>(header.type),
					static_cast<unsigned>(header.version),
					static_cast<unsigned>(header.size),
					n);
				continue;
			}

			const auto type = static_cast<cmp::Msg>(header.type);

			if (type == cmp::Msg::SessionQuery && n >= static_cast<int>(sizeof(cmp::SessionQuery))) {
				if (!cmp::allow_rate(queryRates, key, t, 4)) {
					LOG_DEBUG("rate-limit SessionQuery from %s", key.c_str());
					continue;
				}
				auto* host = find_host(clients, world);
				const bool haveHost = host && host->havePose && world.hostPeerId != 0;
				const bool hostInterior = haveHost && world.hostLocation != 0 && world.hostLocation != cmp::kCommonwealthWorldspace;
				const auto info = cmp::make_session_info(
					haveHost ? world.hostPeerId : 0,
					haveHost ? world.hostLocation : 0,
					haveHost ? world.hostX : 0.0f,
					haveHost ? world.hostY : 0.0f,
					haveHost ? world.hostZ : 0.0f,
					static_cast<std::uint32_t>(clients.size()),
					haveHost,
					hostInterior,
					cfg.name,
					static_cast<std::uint32_t>(cfg.maxPlayers),
					cfg.motd);
				send_to(sock, from, &info, sizeof(info), "SessionInfo");
				LOG_INFO("SessionQuery from %s haveHost=%d host=%u loc=%X clients=%zu/%d name=%s",
					key.c_str(),
					haveHost ? 1 : 0,
					info.hostPeerId,
					info.hostLocationFormId,
					clients.size(),
					cfg.maxPlayers,
					cfg.name.c_str());
				continue;
			}

			if (type == cmp::Msg::Hello && n >= static_cast<int>(sizeof(cmp::Hello))) {
				if (!cmp::allow_rate(helloRates, key, t, 2)) {
					LOG_DEBUG("rate-limit Hello from %s", key.c_str());
					continue;
				}
				cmp::Hello hello{};
				std::memcpy(&hello, buf, sizeof(hello));
				hello.name[31] = '\0';
				hello.playerKey[31] = '\0';

				if (hello.protocol != cmp::kProtocolVersion || header.version != cmp::kProtocolVersion) {
					reject_to(sock, from, cmp::RejectReason::Protocol, "need protocol 6");
					continue;
				}
				if (hello.pluginVersion != cmp::kPluginVersion) {
					reject_to(sock, from, cmp::RejectReason::PluginVersion, "plugin version mismatch");
					continue;
				}
				if (!hello.inWorld) {
					reject_to(sock, from, cmp::RejectReason::NotInWorld, "load a save and enter the world");
					continue;
				}

				const bool requireHost = (hello.flags & cmp::kHelloFlagRequireHost) != 0;
				if (requireHost) {
					auto* liveHost = find_host(clients, world);
					if (!liveHost || !liveHost->havePose || world.hostPeerId == 0) {
						reject_to(sock, from, cmp::RejectReason::NoHost, "need a live host in the world");
						continue;
					}
					if (world.hostLocation != cmp::kCommonwealthWorldspace) {
						reject_to(sock, from, cmp::RejectReason::HostNotStreaming, "host is not Commonwealth exterior");
						continue;
					}
				}

				const auto pkey = cmp::sanitize_player_key(hello.playerKey, "player");
				auto existing = clients.find(key);
				if (existing == clients.end() && static_cast<int>(clients.size()) >= cfg.maxPlayers) {
					reject_to(sock, from, cmp::RejectReason::Full, "server full");
					log_json_event(cfg.jsonLog, "{\"event\":\"full\",\"addr\":\"" + key + "\"}");
					continue;
				}

				auto& rec = players[pkey];
				rec.key = pkey;
				if (rec.name.empty()) {
					rec.name = hello.name[0] ? hello.name : "player";
				}
				const bool isNew = !rec.havePose;
				if (!world.created) {
					world.created = true;
					worldDirty = true;
					LOG_INFO("world created spawn Sanctuary");
				}

				auto it = clients.find(key);
				if (it == clients.end()) {
					Client c;
					c.addr = from;
					c.peerId = nextPeer++;
					if (c.peerId == cmp::kFakePeerId) {
						c.peerId = nextPeer++;
					}
					c.playerKey = pkey;
					c.name = hello.name[0] ? hello.name : rec.name;
					c.lastSeen = t;
					if (isNew) {
						c.lastPose = cmp::make_pose(c.peerId, world.spawnLocation, world.spawnX, world.spawnY, world.spawnZ, 0.0f);
					} else {
						c.lastPose = cmp::make_pose(c.peerId, rec.locationFormId, rec.x, rec.y, rec.z, rec.yaw);
					}
					c.havePose = true;
					c.appearance = rec.appearance;
					c.inventory = rec.inventory;
					it = clients.emplace(key, c).first;
					if (!world.hostPeerId) {
						if (requireHost) {
							clients.erase(it);
							reject_to(sock, from, cmp::RejectReason::NoHost, "need a live host in the world");
							continue;
						}
						world.hostPeerId = c.peerId;
					}
					LOG_INFO("Hello NEW %s key=%s name=%s peer=%u new=%d host=%d clients=%zu",
						key.c_str(),
						pkey.c_str(),
						it->second.name.c_str(),
						it->second.peerId,
						isNew ? 1 : 0,
						it->second.peerId == world.hostPeerId ? 1 : 0,
						clients.size());
					log_json_event(cfg.jsonLog,
						std::string("{\"event\":\"join\",\"peer\":") + std::to_string(it->second.peerId)
							+ ",\"key\":\"" + pkey + "\",\"name\":\"" + it->second.name + "\"}");
				} else {
					it->second.lastSeen = t;
					it->second.addr = from;
					it->second.playerKey = pkey;
					LOG_INFO("Hello again %s key=%s peer %u", key.c_str(), pkey.c_str(), it->second.peerId);
				}

				if (it->second.peerId == world.hostPeerId) {
					world.gameHour = hello.gameHour;
					world.gameDaysPassed = hello.gameDaysPassed;
					world.weatherFormId = hello.weatherFormId;
					world.hostLocation = hello.locationFormId ? hello.locationFormId : world.hostLocation;
					world.hostX = hello.x;
					world.hostY = hello.y;
					world.hostZ = hello.z;
					worldDirty = true;
				}

				const bool fakeNow = fakeWanted && clients.size() < 2;
				const bool isHost = it->second.peerId == world.hostPeerId;
				const auto welcome = cmp::make_welcome(it->second.peerId, fakeNow ? cmp::kFakePeerId : 0, isNew, isHost);
				send_to(sock, from, &welcome, sizeof(welcome), "Welcome");
				LOG_INFO("tx Welcome peer=%u fake=%u new=%d host=%d -> %s",
					welcome.peerId, welcome.fakePeerId, isNew ? 1 : 0, isHost ? 1 : 0, key.c_str());

				std::uint32_t placeLoc = world.hostLocation;
				float px = world.hostX;
				float py = world.hostY;
				float pz = world.hostZ;
				if (requireHost) {
					px += cmp::kGuestSpawnOffsetX;
				} else if (!isNew && rec.havePose && rec.locationFormId != 0 && rec.locationFormId == world.hostLocation) {
					placeLoc = rec.locationFormId;
					px = rec.x;
					py = rec.y;
					pz = rec.z;
				}
				const auto snap = cmp::make_world_snapshot(
					world.gameHour,
					world.gameDaysPassed,
					world.weatherFormId,
					world.hostLocation,
					world.hostX,
					world.hostY,
					world.hostZ,
					world.hostPeerId,
					placeLoc,
					px,
					py,
					pz,
					isNew);
				send_to(sock, from, &snap, sizeof(snap), "WorldSnapshot");

				for (auto& [_, other] : clients) {
					if (same_addr(other.addr, from) || !other.havePose) {
						continue;
					}
					send_to(sock, from, &other.lastPose, sizeof(other.lastPose), "PlayerPose");
					if (!other.appearance.empty()) {
						send_blob(sock, from, cmp::Msg::AppearanceChunk, other.peerId, other.appearance);
					}
					if (!other.inventory.empty()) {
						send_blob(sock, from, cmp::Msg::InventoryChunk, other.peerId, other.inventory);
					}
				}

				if (fakeWasOn && clients.size() >= 2) {
					const auto byeFake = cmp::make_bye(cmp::kFakePeerId);
					for (auto& [_, client] : clients) {
						send_to(sock, client.addr, &byeFake, sizeof(byeFake), "ByeFake");
					}
					LOG_INFO("fake peer off (clients=%zu)", clients.size());
				}
				fakeWasOn = fakeNow;
				continue;
			}

			if (type == cmp::Msg::Bye) {
				auto it = clients.find(key);
				LOG_INFO("Bye from %s peer=%u", key.c_str(), it == clients.end() ? 0 : it->second.peerId);
				if (it != clients.end()) {
					log_json_event(cfg.jsonLog,
						std::string("{\"event\":\"leave\",\"peer\":") + std::to_string(it->second.peerId)
							+ ",\"key\":\"" + it->second.playerKey + "\"}");
					if (auto pit = players.find(it->second.playerKey); pit != players.end()) {
						dirtyPlayers.insert(pit->first);
					}
					if (it->second.peerId == world.hostPeerId) {
						world.hostPeerId = 0;
					}
					const auto bye = cmp::make_bye(it->second.peerId);
					for (auto& [_, other] : clients) {
						if (!same_addr(other.addr, from)) {
							send_to(sock, other.addr, &bye, sizeof(bye), "Bye");
						}
					}
					clients.erase(it);
					find_host(clients, world);
					flush_dirty(world, worldDirty, players, dirtyPlayers);
				}
				continue;
			}

			if (type == cmp::Msg::WorldSnapshot && n >= static_cast<int>(sizeof(cmp::WorldSnapshot))) {
				cmp::WorldSnapshot snap{};
				std::memcpy(&snap, buf, sizeof(snap));
				auto it = clients.find(key);
				if (it == clients.end()) {
					continue;
				}
				it->second.lastSeen = t;
				if (it->second.peerId == world.hostPeerId) {
					world.gameHour = snap.gameHour;
					world.gameDaysPassed = snap.gameDaysPassed;
					world.weatherFormId = snap.weatherFormId;
					world.hostLocation = snap.hostLocationFormId ? snap.hostLocationFormId : world.hostLocation;
					world.hostX = snap.hostX;
					world.hostY = snap.hostY;
					world.hostZ = snap.hostZ;
					worldDirty = true;
					LOG_INFO("WorldSnapshot from host peer %u hour=%.2f days=%.2f",
						it->second.peerId, snap.gameHour, snap.gameDaysPassed);
				}
				continue;
			}

			if (type == cmp::Msg::AppearanceChunk && n >= static_cast<int>(sizeof(cmp::BlobChunk))) {
				cmp::BlobChunk chunk{};
				std::memcpy(&chunk, buf, sizeof(chunk));
				auto it = clients.find(key);
				if (it == clients.end()) {
					continue;
				}
				it->second.lastSeen = t;
				std::vector<std::uint8_t> blob;
				if (take_blob(appearAsm[it->second.peerId], buf, n, blob)) {
					appearAsm.erase(it->second.peerId);
					it->second.appearance = blob;
					auto& rec = players[it->second.playerKey];
					rec.key = it->second.playerKey;
					rec.appearance = blob;
					dirtyPlayers.insert(rec.key);
					for (auto& [_, other] : clients) {
						if (!same_addr(other.addr, from)) {
							send_blob(sock, other.addr, cmp::Msg::AppearanceChunk, it->second.peerId, blob);
						}
					}
					LOG_INFO("Appearance peer=%u key=%s bytes=%zu", it->second.peerId, it->second.playerKey.c_str(), blob.size());
				}
				continue;
			}

			if (type == cmp::Msg::InventoryChunk && n >= static_cast<int>(sizeof(cmp::BlobChunk))) {
				cmp::BlobChunk chunk{};
				std::memcpy(&chunk, buf, sizeof(chunk));
				auto it = clients.find(key);
				if (it == clients.end()) {
					continue;
				}
				it->second.lastSeen = t;
				std::vector<std::uint8_t> blob;
				if (take_blob(invAsm[it->second.peerId], buf, n, blob)) {
					invAsm.erase(it->second.peerId);
					it->second.inventory = blob;
					auto& rec = players[it->second.playerKey];
					rec.key = it->second.playerKey;
					rec.inventory = blob;
					dirtyPlayers.insert(rec.key);
					for (auto& [_, other] : clients) {
						if (!same_addr(other.addr, from)) {
							send_blob(sock, other.addr, cmp::Msg::InventoryChunk, it->second.peerId, blob);
						}
					}
					LOG_INFO("Inventory peer=%u key=%s bytes=%zu", it->second.peerId, it->second.playerKey.c_str(), blob.size());
				}
				continue;
			}

			if (type == cmp::Msg::PlayerPose && n >= static_cast<int>(sizeof(cmp::PlayerPose))) {
				cmp::PlayerPose pose{};
				std::memcpy(&pose, buf, sizeof(pose));
				auto it = clients.find(key);
				if (it == clients.end()) {
					LOG_DEBUG("PlayerPose from unknown %s", key.c_str());
					continue;
				}
				it->second.lastSeen = t;
				++it->second.posesIn;
				pose.peerId = it->second.peerId;
				cmp::fill_header(pose, cmp::Msg::PlayerPose);
				it->second.lastPose = pose;
				it->second.havePose = true;
				auto& rec = players[it->second.playerKey];
				rec.key = it->second.playerKey;
				rec.havePose = true;
				rec.locationFormId = pose.locationFormId;
				rec.x = pose.x;
				rec.y = pose.y;
				rec.z = pose.z;
				rec.yaw = pose.yaw;
				dirtyPlayers.insert(rec.key);
				if (it->second.peerId == world.hostPeerId) {
					world.hostLocation = pose.locationFormId ? pose.locationFormId : world.hostLocation;
					world.hostX = pose.x;
					world.hostY = pose.y;
					world.hostZ = pose.z;
					worldDirty = true;
				}
				for (auto& [_, other] : clients) {
					if (same_addr(other.addr, from)) {
						continue;
					}
					if (!in_interest(it->second, other, cfg.interestUu)) {
						continue;
					}
					send_to(sock, other.addr, &pose, sizeof(pose), "PlayerPose");
				}
				continue;
			}

			LOG_DEBUG("unhandled msg type=%u from %s", static_cast<unsigned>(header.type), key.c_str());
		}

		for (auto it = clients.begin(); it != clients.end();) {
			if (t - it->second.lastSeen > kClientTimeoutSec) {
				LOG_INFO("timeout %s peer=%u", it->first.c_str(), it->second.peerId);
				if (auto pit = players.find(it->second.playerKey); pit != players.end()) {
					dirtyPlayers.insert(pit->first);
				}
				if (it->second.peerId == world.hostPeerId) {
					world.hostPeerId = 0;
				}
				it = clients.erase(it);
				find_host(clients, world);
				flush_dirty(world, worldDirty, players, dirtyPlayers);
			} else {
				++it;
			}
		}

		if (fakeActive && (t - lastFake) >= (1.0 / kFakeHz)) {
			lastFake = t;
			fakeWasOn = true;
			fakeAngle += 0.35 / kFakeHz;
			if (fakeAngle > 6.28318530718) {
				fakeAngle -= 6.28318530718;
			}
			float cx = world.spawnX;
			float cy = world.spawnY;
			float cz = world.spawnZ;
			std::uint32_t loc = world.spawnLocation;
			if (auto* host = find_host(clients, world); host && host->havePose) {
				cx = host->lastPose.x;
				cy = host->lastPose.y;
				cz = host->lastPose.z;
				loc = host->lastPose.locationFormId ? host->lastPose.locationFormId : loc;
			}
			const float radius = 250.0f;
			const float omega = 0.35f;
			const float x = cx + radius * static_cast<float>(std::cos(fakeAngle));
			const float y = cy + radius * static_cast<float>(std::sin(fakeAngle));
			const float speed = omega * radius;
			const float vx = -static_cast<float>(std::sin(fakeAngle)) * speed;
			const float vy = static_cast<float>(std::cos(fakeAngle)) * speed;
			const auto pose = cmp::make_pose(
				cmp::kFakePeerId,
				loc,
				x,
				y,
				cz,
				static_cast<float>(fakeAngle + 1.57079632679),
				0.0f,
				speed,
				vx,
				vy,
				0);
			for (auto& [_, client] : clients) {
				send_to(sock, client.addr, &pose, sizeof(pose), "FakePose");
			}
			if ((++poseLog % kFakeHz) == 0) {
				LOG_DEBUG("fake peer tick x=%.1f y=%.1f clients=%zu datagrams=%llu bad=%llu",
					x, y, clients.size(),
					static_cast<unsigned long long>(datagrams),
					static_cast<unsigned long long>(badHeaders));
			}
		}

		if (t - lastPersist >= kPersistIntervalSec) {
			lastPersist = t;
			flush_dirty(world, worldDirty, players, dirtyPlayers);
		}

		if (t - lastStatusBar >= kStatusBarIntervalSec) {
			lastStatusBar = t;
			sample_proc_stats(procStats);
			char bar[256]{};
			std::snprintf(
				bar,
				sizeof(bar),
				"CMP %s :%u | %zu/%d | host %u | fake %s | rx %llu | cpu %.0f%% | mem %.0fMB",
				kServerVersion,
				static_cast<unsigned>(cfg.port),
				clients.size(),
				cfg.maxPlayers,
				world.hostPeerId,
				fakeWanted ? "on" : "off",
				static_cast<unsigned long long>(datagrams),
				procStats.cpuPercent,
				procStats.memMb);
			if (!cfg.quiet) {
				ServerLog::instance().set_status(bar);
			}
			if (t - lastStatusLog >= kStatusLogIntervalSec) {
				lastStatusLog = t;
				LOG_DEBUG("status name=%s port=%u clients=%zu/%d host=%u fake=%s datagrams=%llu bad=%llu cpu=%.1f mem=%.1fMB",
					cfg.name.c_str(),
					static_cast<unsigned>(cfg.port),
					clients.size(),
					cfg.maxPlayers,
					world.hostPeerId,
					fakeWanted ? "on" : "off",
					static_cast<unsigned long long>(datagrams),
					static_cast<unsigned long long>(badHeaders),
					procStats.cpuPercent,
					procStats.memMb);
			}
		}

		WSAPOLLFD pfd{};
		pfd.fd = sock;
		pfd.events = POLLIN;
		WSAPoll(&pfd, 1, 5);
	}

	ServerLog::instance().set_status_enabled(false);
	admin.stop();
	flush_dirty(world, worldDirty, players, dirtyPlayers);
	for (const auto& [_, rec] : players) {
		persist_player(rec);
	}
	persist_world(world);
	ServerLog::instance().flush();
	LOG_INFO("closing socket clients=%zu datagrams=%llu badHeaders=%llu",
		clients.size(),
		static_cast<unsigned long long>(datagrams),
		static_cast<unsigned long long>(badHeaders));
	closesocket(sock);
	WSACleanup();
	LOG_INFO("stopped");
	return 0;
}
