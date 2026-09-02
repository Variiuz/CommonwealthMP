#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "admin.hpp"
#ifdef _WIN32
#include "win_console.hpp"
#endif
#include "cmp_udp.hpp"
#include "config.hpp"
#include "handlers.hpp"
#include "log.hpp"
#include "server_state.hpp"

std::atomic<bool> g_running{ true };

#ifdef _WIN32
namespace {
WinConsole g_winConsole;

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

bool relaunch_in_classic_console()
{
	char probe[64]{};
	if (GetEnvironmentVariableA("CMP_CONHOST", probe, static_cast<DWORD>(sizeof(probe))) > 0) return false;
	wchar_t sysDir[MAX_PATH]{};
	if (GetSystemDirectoryW(sysDir, MAX_PATH) == 0) return false;
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
	const BOOL ok = CreateProcessW(conhost.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);
	if (!ok) {
		SetEnvironmentVariableA("CMP_CONHOST", nullptr);
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}
}
#else
namespace {
void on_posix_stop(int) { g_running = false; }
}
#endif

int main(int argc, char** argv)
{
	bool winConsoleActive = false;
#ifdef _WIN32
	char probe[64]{};
	const bool useClassicConsole = GetEnvironmentVariableA("CMP_CONHOST", probe, static_cast<DWORD>(sizeof(probe))) > 0;
	if (!useClassicConsole) {
		g_winConsole.set_shutdown_callback([] { g_running = false; });
		winConsoleActive = g_winConsole.start();
		if (!winConsoleActive && relaunch_in_classic_console()) return 0;
	} else if (relaunch_in_classic_console()) return 0;
#endif
	if (!winConsoleActive) ServerLog::instance().ensure_console();
	bool useAdminConsole = true;
#ifdef _WIN32
	useAdminConsole = !winConsoleActive;
	SetConsoleCtrlHandler(on_ctrl, TRUE);
	SetUnhandledExceptionFilter(on_crash);
#else
	std::signal(SIGINT, on_posix_stop);
	std::signal(SIGTERM, on_posix_stop);
#endif

	ServerConfig cfg;
	cfg.logFile = (fs::path(exe_dir()) / "CommonwealthMP.Server.log").string();
	std::string configPath = (fs::path(exe_dir()) / "server.ini").string();
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--config" && i + 1 < argc) { configPath = argv[++i]; cfg.configPath = configPath; }
	}
	bool createdIni = false;
	ensure_server_ini(configPath, cfg, &createdIni);
	if (cfg.logFile.empty()) cfg.logFile = (fs::path(exe_dir()) / "CommonwealthMP.Server.log").string();
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--help" || arg == "-h") { print_usage(); return 0; }
		if (arg == "--config" && i + 1 < argc) { ++i; continue; }
		if (arg == "--fake") { cfg.fake = true; continue; }
		if (arg == "--no-fake") { cfg.fake = false; continue; }
		if (arg == "--fake-count" && i + 1 < argc) { cfg.fakeCount = cmp::clamp_fake_count(std::stoi(argv[++i])); continue; }
		if (arg == "--reset-session") { cfg.resetSession = true; continue; }
		if (arg == "--verbose") { cfg.verbose = true; cfg.quiet = false; continue; }
		if (arg == "--quiet") { cfg.quiet = true; cfg.verbose = false; continue; }
		if (arg == "--port" && i + 1 < argc) { cfg.port = static_cast<std::uint16_t>(std::stoi(argv[++i])); continue; }
		if (arg == "--max-players" && i + 1 < argc) { cfg.maxPlayers = std::max(1, std::stoi(argv[++i])); continue; }
		if (arg == "--log-file" && i + 1 < argc) { cfg.logFile = argv[++i]; continue; }
		if (arg == "--session-dir" && i + 1 < argc) { cfg.sessionDir = argv[++i]; continue; }
		if (arg == "--name" && i + 1 < argc) { cfg.name = argv[++i]; continue; }
		if (arg == "--motd" && i + 1 < argc) { cfg.motd = argv[++i]; continue; }
		if (arg == "--interest-uu" && i + 1 < argc) { cfg.interestUu = std::max(0.0f, std::stof(argv[++i])); continue; }
		if (arg == "--json-log") { cfg.jsonLog = true; continue; }
		if (arg == "--pvp") { cfg.pvp = true; continue; }
		if (arg == "--no-pvp") { cfg.pvp = false; continue; }
		if (arg == "--password" && i + 1 < argc) { cfg.password = argv[++i]; if (cfg.password.size() > 15) cfg.password.resize(15); continue; }
		if (arg == "--mod-hash" && i + 1 < argc) { cfg.modHash = parse_mod_hash_ini(argv[++i]); continue; }
		if (arg == "--ban" && i + 1 < argc) { cfg.banKeys.push_back(argv[++i]); continue; }
		LOG_ERROR("Unknown arg: %s", arg.c_str());
		print_usage();
		return 2;
	}

	ServerLog::instance().set_level(cfg.verbose ? LogLevel::Debug : (cfg.quiet ? LogLevel::Warn : LogLevel::Info), LogLevel::Debug);
	if (!ServerLog::instance().open_file(cfg.logFile)) LOG_ERROR("Could not open log file %s", cfg.logFile.c_str());
	ServerLog::instance().set_title("CMP " + cfg.name);
	if (!cfg.sessionDir.empty()) set_session_dir(cfg.sessionDir);
	print_banner();
	LOG_INFO("CommonwealthMP.Server %s (%s) starting", kServerVersion, CMP_GIT_VERSION);
	if (createdIni) LOG_INFO("wrote default config %s", configPath.c_str());
	LOG_INFO("config=%s", configPath.c_str());
	LOG_INFO("name=%s port=%u max_players=%d fake=%s fake_count=%d interest_uu=%.0f pvp=%s", cfg.name.c_str(), static_cast<unsigned>(cfg.port), cfg.maxPlayers, cfg.fake ? "on" : "off", cfg.fakeCount, cfg.interestUu, cfg.pvp ? "on" : "off");
	if (!cfg.motd.empty()) LOG_INFO("motd=%s", cfg.motd.c_str());
	LOG_INFO("session=%s", session_dir().string().c_str());
	LOG_INFO("log file: %s (DEBUG in file, %s on console%s)", ServerLog::instance().path().c_str(), cfg.verbose ? "DEBUG" : (cfg.quiet ? "WARN+" : "INFO"), cfg.jsonLog ? ", json events" : "");
	LOG_DEBUG("protocol magic=CMP1 version=%u plugin=%u defaultPort=%u config=%s", static_cast<unsigned>(cmp::kProtocolVersion), cmp::kPluginVersion, static_cast<unsigned>(cmp::kDefaultPort), configPath.c_str());
	if (cfg.resetSession) { std::error_code ec; fs::remove_all(session_dir(), ec); LOG_INFO("session folder cleared"); }
	else backup_session_folder();
	SessionWorld world;
	std::unordered_map<std::string, PlayerRec> players;
	load_world(world, players);
	std::unordered_set<std::string> bannedKeys;
	load_bans(bannedKeys);
	for (const auto& banKey : cfg.banKeys) bannedKeys.insert(cmp::sanitize_player_key(banKey, "player"));
	if (cfg.modHash != 0) LOG_INFO("mod_hash pinned 0x%08X", cfg.modHash);
	if (!cfg.password.empty()) LOG_INFO("password gate enabled");
	if (!cmp::udp_startup()) { LOG_ERROR("socket startup failed err=%d", cmp::udp_last_error()); return 1; }
	CmpSocket sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == kCmpInvalidSocket) { LOG_ERROR("socket failed err=%d", cmp::udp_last_error()); cmp::udp_cleanup(); return 1; }
	int reuse = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
	sockaddr_in bindAddr{};
	bindAddr.sin_family = AF_INET; bindAddr.sin_addr.s_addr = htonl(INADDR_ANY); bindAddr.sin_port = htons(cfg.port);
	if (bind(sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) { LOG_ERROR("bind failed on UDP %u err=%d", static_cast<unsigned>(cfg.port), cmp::udp_last_error()); cmp::udp_close(sock); cmp::udp_cleanup(); return 1; }
	if (!cmp::udp_set_nonblock(sock)) { LOG_ERROR("nonblock failed err=%d", cmp::udp_last_error()); cmp::udp_close(sock); cmp::udp_cleanup(); return 1; }
	LOG_INFO("listening UDP 0.0.0.0:%u", static_cast<unsigned>(cfg.port));
	LOG_INFO("fake peers: %s count=%d (off at 2 clients)", cfg.fake ? "on" : "off", cfg.fakeCount);
	LOG_INFO("Join from FO4: load any save, cmp_join 127 0 0 1 %u (guests meet host exterior). Menu join requires a live host.", static_cast<unsigned>(cfg.port));
	LOG_INFO("Type 'help' for admin commands");

	ServerRuntime runtime(sock, cfg, world, players, bannedKeys, cfg.modHash);
	FakeTickState fakeState{ now_sec() };
	auto lastPersist = now_sec(), lastStatusBar = now_sec(), lastStatusLog = now_sec(), lastHeartbeat = now_sec();
	ProcStats procStats{};
	sample_proc_stats(procStats);
	if (!cfg.quiet) {
		ServerLog::instance().set_status_enabled(true);
		char bar[256]{};
		std::snprintf(bar, sizeof(bar), "CMP %s :%u | %zu/%d | host %u | fake %s | rx %llu | cpu %.0f%% | mem %.0fMB", kServerVersion, static_cast<unsigned>(cfg.port), static_cast<std::size_t>(0), cfg.maxPlayers, world.hostPeerId, runtime.fakeWanted ? "on" : "off", static_cast<unsigned long long>(0), procStats.cpuPercent, procStats.memMb);
		ServerLog::instance().set_status(bar);
#ifdef _WIN32
		if (winConsoleActive) g_winConsole.set_title(bar);
#endif
	}
	AdminConsole admin;
	if (useAdminConsole) admin.start();
	AdminContext adminCtx{ runtime, configPath };
	while (g_running) {
		const double t = now_sec();
		const bool fakeActive = runtime.fakeWanted && runtime.clients.size() < 2;
		std::string cmdLine;
#ifdef _WIN32
		while (useAdminConsole ? admin.poll(cmdLine) : g_winConsole.poll(cmdLine)) run_admin_command(cmdLine, adminCtx);
#else
		while (admin.poll(cmdLine)) run_admin_command(cmdLine, adminCtx);
#endif
		for (;;) {
			char buf[kMaxDatagram]{};
			sockaddr_in from{};
			CmpSockLen fromLen = sizeof(from);
			const int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
			if (n == SOCKET_ERROR) { const int err = cmp::udp_last_error(); if (!cmp::udp_would_block(err)) LOG_WARN("recvfrom err=%d", err); break; }
			if (n == 0) break;
			runtime.handle_packet(buf, n, from, t);
		}
		runtime.expire_clients(t);
		runtime.tick_reliable(t);
		if (fakeActive) tick_fake_peers(sock, runtime.clients, world, runtime.fakeCount, t, runtime.datagrams, runtime.badHeaders, runtime.fakeWasOn, fakeState);
		if (t - lastPersist >= kPersistIntervalSec) { lastPersist = t; flush_dirty(world, runtime.worldDirty, players, runtime.dirtyPlayers); }
		if (t - lastHeartbeat >= 5.0) { lastHeartbeat = t; runtime.send_heartbeat(); }
		if (t - lastStatusBar >= kStatusBarIntervalSec) {
			lastStatusBar = t; sample_proc_stats(procStats);
			char bar[256]{};
			std::snprintf(bar, sizeof(bar), "CMP %s :%u | %zu/%d | host %u | fake %s | rx %llu | cpu %.0f%% | mem %.0fMB", kServerVersion, static_cast<unsigned>(cfg.port), runtime.clients.size(), cfg.maxPlayers, world.hostPeerId, runtime.fakeWanted ? "on" : "off", static_cast<unsigned long long>(runtime.datagrams), procStats.cpuPercent, procStats.memMb);
			if (!cfg.quiet) { ServerLog::instance().set_status(bar);
#ifdef _WIN32
				if (winConsoleActive) g_winConsole.set_title(bar);
#endif
			}
			if (t - lastStatusLog >= kStatusLogIntervalSec) { lastStatusLog = t; LOG_DEBUG("status name=%s port=%u clients=%zu/%d host=%u fake=%s datagrams=%llu bad=%llu cpu=%.1f mem=%.1fMB", cfg.name.c_str(), static_cast<unsigned>(cfg.port), runtime.clients.size(), cfg.maxPlayers, world.hostPeerId, runtime.fakeWanted ? "on" : "off", static_cast<unsigned long long>(runtime.datagrams), static_cast<unsigned long long>(runtime.badHeaders), procStats.cpuPercent, procStats.memMb); }
		}
		cmp::udp_poll_in(sock, 5);
	}
	ServerLog::instance().set_status_enabled(false);
	if (useAdminConsole) admin.stop();
#ifdef _WIN32
	g_winConsole.stop();
#endif
	flush_dirty(world, runtime.worldDirty, players, runtime.dirtyPlayers);
	for (const auto& [_, rec] : players) persist_player(rec);
	persist_world(world);
	ServerLog::instance().flush();
	LOG_INFO("closing socket clients=%zu datagrams=%llu badHeaders=%llu", runtime.clients.size(), static_cast<unsigned long long>(runtime.datagrams), static_cast<unsigned long long>(runtime.badHeaders));
	cmp::udp_close(sock); cmp::udp_cleanup();
	LOG_INFO("stopped");
	return 0;
}
