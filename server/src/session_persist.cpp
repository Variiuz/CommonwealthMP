#include "server_state.hpp"

#include <fstream>
#include <sstream>

#include "cmp_json.hpp"
#include "log.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

fs::path g_sessionDir;

std::string exe_dir()
{
#ifdef _WIN32
	char path[MAX_PATH]{};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	return fs::path(path).parent_path().string();
#else
	char path[4096]{};
	const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (n <= 0) return ".";
	path[n] = '\0';
	return fs::path(path).parent_path().string();
#endif
}

fs::path session_dir()
{
	if (!g_sessionDir.empty()) {
		return g_sessionDir;
	}
	return fs::path(exe_dir()) / "session";
}

void set_session_dir(const fs::path& path)
{
	g_sessionDir = path;
}

void persist_world(const SessionWorld& world)
{
	std::error_code ec;
	fs::create_directories(session_dir() / "players", ec);
	std::ofstream json(session_dir() / "world.json", std::ios::trunc);
	if (!json) return;
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
	if (rec.key.empty()) return;
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

void flush_dirty(SessionWorld& world, bool& worldDirty, std::unordered_map<std::string, PlayerRec>& players, std::unordered_set<std::string>& dirtyPlayers)
{
	if (worldDirty) {
		persist_world(world);
		worldDirty = false;
	}
	for (const auto& key : dirtyPlayers) {
		if (auto it = players.find(key); it != players.end()) persist_player(it->second);
	}
	dirtyPlayers.clear();
}

namespace {
std::vector<std::uint8_t> read_bin(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in) return {};
	const auto pos = in.tellg();
	if (pos < 0 || pos > static_cast<std::streamoff>(1 << 20)) return {};
	const auto n = static_cast<std::size_t>(pos);
	in.seekg(0);
	std::vector<std::uint8_t> out(n);
	in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
	return out;
}
}

void load_world(SessionWorld& world, std::unordered_map<std::string, PlayerRec>& players)
{
	std::ifstream in(session_dir() / "world.json");
	if (in) {
		std::stringstream ss;
		ss << in.rdbuf();
		const auto text = ss.str();
		world.created = true;
		world.spawnLocation = static_cast<std::uint32_t>(cmp::json_number(text, "spawnLocation", world.spawnLocation));
		world.spawnX = static_cast<float>(cmp::json_number(text, "spawnX", world.spawnX));
		world.spawnY = static_cast<float>(cmp::json_number(text, "spawnY", world.spawnY));
		world.spawnZ = static_cast<float>(cmp::json_number(text, "spawnZ", world.spawnZ));
		world.gameHour = static_cast<float>(cmp::json_number(text, "gameHour", world.gameHour));
		world.gameDaysPassed = static_cast<float>(cmp::json_number(text, "gameDaysPassed", world.gameDaysPassed));
		world.weatherFormId = static_cast<std::uint32_t>(cmp::json_number(text, "weatherFormId", 0));
		LOG_INFO("world.json loaded spawn=(%.0f,%.0f,%.0f)", world.spawnX, world.spawnY, world.spawnZ);
	}
	std::error_code ec;
	const auto dir = session_dir() / "players";
	if (!fs::exists(dir, ec)) return;
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
		std::ifstream pf(entry.path());
		if (!pf) continue;
		std::stringstream ss;
		ss << pf.rdbuf();
		const auto text = ss.str();
		PlayerRec rec;
		rec.key = cmp::json_quoted(text, "key");
		if (rec.key.empty()) rec.key = entry.path().stem().string();
		rec.name = cmp::json_quoted(text, "name");
		rec.havePose = cmp::json_number(text, "havePose", 0) != 0;
		rec.locationFormId = static_cast<std::uint32_t>(cmp::json_number(text, "locationFormId", rec.locationFormId));
		rec.x = static_cast<float>(cmp::json_number(text, "x", rec.x));
		rec.y = static_cast<float>(cmp::json_number(text, "y", rec.y));
		rec.z = static_cast<float>(cmp::json_number(text, "z", rec.z));
		rec.yaw = static_cast<float>(cmp::json_number(text, "yaw", 0));
		rec.appearance = read_bin(dir / (rec.key + ".appearance.bin"));
		rec.inventory = read_bin(dir / (rec.key + ".inventory.bin"));
		players[rec.key] = rec;
		LOG_INFO("player %s loaded pose=%d appear=%zu inv=%zu", rec.key.c_str(), rec.havePose ? 1 : 0, rec.appearance.size(), rec.inventory.size());
	}
}

void load_bans(std::unordered_set<std::string>& bans)
{
	bans.clear();
	std::ifstream in(session_dir() / "bans.json");
	if (!in) return;
	std::stringstream ss;
	ss << in.rdbuf();
	const auto text = ss.str();
	for (std::size_t i = 0; i < text.size();) {
		const auto q1 = text.find('"', i);
		if (q1 == std::string::npos) break;
		const auto q2 = text.find('"', q1 + 1);
		if (q2 == std::string::npos) break;
		const auto key = cmp::sanitize_player_key(text.substr(q1 + 1, q2 - q1 - 1), {});
		if (!key.empty()) bans.insert(key);
		i = q2 + 1;
	}
}

void persist_bans(const std::unordered_set<std::string>& bans)
{
	std::error_code ec;
	fs::create_directories(session_dir(), ec);
	std::ofstream out(session_dir() / "bans.json", std::ios::trunc);
	if (!out) return;
	out << "[\n";
	bool first = true;
	for (const auto& key : bans) {
		if (!first) out << ",\n";
		out << "  \"" << key << "\"";
		first = false;
	}
	out << "\n]\n";
}

void backup_session_folder()
{
	const auto src = session_dir();
	std::error_code ec;
	if (!fs::exists(src, ec)) return;
	const auto dst = src.parent_path() / (src.filename().string() + ".bak");
	fs::remove_all(dst, ec);
	fs::copy(src, dst, fs::copy_options::recursive, ec);
	if (ec) LOG_WARN("session backup failed: %s", ec.message().c_str());
	else LOG_INFO("session backed up to %s", dst.string().c_str());
}
