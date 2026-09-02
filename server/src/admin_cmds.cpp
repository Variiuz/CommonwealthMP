#include "handlers.hpp"

#include <algorithm>

#include "log.hpp"

void run_admin_command(const std::string& cmdLine, AdminContext& ctx)
{
	auto& r = ctx.runtime;
	auto& cfg = r.cfg;
	auto& clients = r.clients;
	auto& players = r.players;
	auto& bannedKeys = r.bannedKeys;
	auto& world = r.world;
	auto& dirtyPlayers = r.dirtyPlayers;
	const auto words = split_words(cmdLine);
	if (words.empty()) return;
	const auto cmd = lower_copy(words[0]);
	const double t = now_sec();
	if (cmd == "help" || cmd == "?") {
		LOG_INFO("commands: help status players kick <peer|key> [ban] say TEXT save quit fake on|off|N maxplayers N reload motd TEXT pvp on|off password TEXT ban|unban|bans");
	} else if (cmd == "status") {
		LOG_INFO("version=%s git=%s", kServerVersion, CMP_GIT_VERSION);
		LOG_INFO("status name=%s port=%u clients=%zu/%d host=%u fake=%s count=%d interest=%.0f pvp=%s modHash=0x%08X datagrams=%llu bad=%llu",
			cfg.name.c_str(), static_cast<unsigned>(cfg.port), clients.size(), cfg.maxPlayers, world.hostPeerId,
			r.fakeWanted ? "on" : "off", r.fakeCount, cfg.interestUu, cfg.pvp ? "on" : "off", r.sessionModHash,
			static_cast<unsigned long long>(r.datagrams), static_cast<unsigned long long>(r.badHeaders));
		for (const auto& [ak, c] : clients) {
			LOG_INFO("  peer=%u key=%s name=%s addr=%s lastSeen=%.1f poses=%llu appear=%zu inv=%zu",
				c.peerId, c.playerKey.c_str(), c.name.c_str(), ak.c_str(), t - c.lastSeen,
				static_cast<unsigned long long>(c.posesIn), c.appearance.size(), c.inventory.size());
		}
	} else if (cmd == "players") {
		if (clients.empty()) LOG_INFO("players: (none)");
		for (const auto& [ak, c] : clients) {
			LOG_INFO("player peer=%u key=%s name=%s addr=%s poses=%llu last=%.1fs",
				c.peerId, c.playerKey.c_str(), c.name.c_str(), ak.c_str(),
				static_cast<unsigned long long>(c.posesIn), t - c.lastSeen);
		}
	} else if (cmd == "kick" && words.size() >= 2) {
		const auto& target = words[1];
		const bool doBan = words.size() >= 3 && lower_copy(words[2]) == "ban";
		bool kicked = false;
		for (auto it = clients.begin(); it != clients.end();) {
			if (std::to_string(it->second.peerId) != target && it->second.playerKey != target) {
				++it;
				continue;
			}
			if (doBan) {
				bannedKeys.insert(it->second.playerKey);
				persist_bans(bannedKeys);
				log_json_event(cfg.jsonLog, std::string("{\"event\":\"ban\",\"key\":\"") + it->second.playerKey + "\"}");
				LOG_INFO("ban key=%s", it->second.playerKey.c_str());
			}
			LOG_INFO("kick peer=%u key=%s", it->second.peerId, it->second.playerKey.c_str());
			log_json_event(cfg.jsonLog, std::string("{\"event\":\"kick\",\"peer\":") + std::to_string(it->second.peerId) + ",\"key\":\"" + it->second.playerKey + "\"}");
			const auto bye = cmp::make_bye(it->second.peerId);
			r.send_reliable(it->second, &bye, sizeof(bye), "ByeKick");
			if (auto pit = players.find(it->second.playerKey); pit != players.end()) dirtyPlayers.insert(pit->first);
			if (it->second.peerId == world.hostPeerId) world.hostPeerId = 0;
			it = clients.erase(it);
			find_host(clients, world);
			kicked = true;
			break;
		}
		if (!kicked) LOG_WARN("kick: no client matching %s", target.c_str());
	} else if (cmd == "say" && words.size() >= 2) {
		std::string text;
		for (std::size_t i = 1; i < words.size(); ++i) {
			if (i > 1) text += " ";
			text += words[i];
		}
		const auto chat = cmp::make_chat(0, text);
		for (auto& [_, c] : clients) r.send_reliable(c, &chat, sizeof(chat), "Chat");
		LOG_INFO("Server chat: %s", text.c_str());
	} else if (cmd == "save") {
		for (const auto& [_, c] : clients) dirtyPlayers.insert(c.playerKey);
		r.worldDirty = true;
		flush_dirty(world, r.worldDirty, players, dirtyPlayers);
		LOG_INFO("session saved");
	} else if (cmd == "quit" || cmd == "exit") {
		LOG_INFO("quit requested");
		g_running = false;
	} else if (cmd == "fake" && words.size() >= 2) {
		const auto arg = lower_copy(words[1]);
		if (arg == "on") {
			r.fakeWanted = true;
			LOG_INFO("fake peer: on count=%d", r.fakeCount);
		} else if (arg == "off") {
			r.fakeWanted = false;
			if (r.fakeWasOn) {
				send_bye_fakes(r.sock, clients, r.fakeCount);
				r.fakeWasOn = false;
			}
			LOG_INFO("fake peer: off");
		} else try {
			r.fakeCount = cmp::clamp_fake_count(std::stoi(words[1]));
			cfg.fakeCount = r.fakeCount;
			r.fakeWanted = true;
			LOG_INFO("fake peer: on count=%d", r.fakeCount);
		} catch (...) { LOG_WARN("usage: fake on|off|N"); }
	} else if (cmd == "maxplayers" && words.size() >= 2) {
		try {
			cfg.maxPlayers = std::max(1, std::stoi(words[1]));
			LOG_INFO("max_players=%d", cfg.maxPlayers);
		} catch (...) { LOG_WARN("usage: maxplayers N"); }
	} else if (cmd == "reload") {
		ServerConfig reloaded = cfg;
		if (load_server_ini(std::string(ctx.configPath), reloaded)) {
			cfg.name = reloaded.name;
			cfg.motd = reloaded.motd;
			cfg.maxPlayers = reloaded.maxPlayers;
			cfg.fake = reloaded.fake;
			cfg.fakeCount = cmp::clamp_fake_count(reloaded.fakeCount);
			r.fakeCount = cfg.fakeCount;
			cfg.interestUu = reloaded.interestUu;
			cfg.jsonLog = reloaded.jsonLog;
			cfg.pvp = reloaded.pvp;
			cfg.password = reloaded.password;
			if (cfg.password.size() > 15) cfg.password.resize(15);
			if (reloaded.modHash != 0) {
				r.sessionModHash = reloaded.modHash;
				cfg.modHash = reloaded.modHash;
			}
			r.fakeWanted = cfg.fake;
			ServerLog::instance().set_title("CMP " + cfg.name);
			r.broadcast_session_rules();
			LOG_INFO("reloaded %s name=%s max=%d motd=%s pvp=%s", std::string(ctx.configPath).c_str(), cfg.name.c_str(), cfg.maxPlayers, cfg.motd.c_str(), cfg.pvp ? "on" : "off");
		} else LOG_WARN("reload failed: could not read %s", std::string(ctx.configPath).c_str());
	} else if (cmd == "pvp") {
		if (words.size() < 2) LOG_INFO("pvp=%s", cfg.pvp ? "on" : "off");
		else {
			const auto arg = lower_copy(words[1]);
			if (arg == "on") cfg.pvp = true;
			else if (arg == "off") cfg.pvp = false;
			else {
				LOG_WARN("usage: pvp on|off");
				return;
			}
			r.broadcast_session_rules();
			LOG_INFO("pvp=%s", cfg.pvp ? "on" : "off");
		}
	} else if (cmd == "password") {
		if (words.size() < 2) LOG_INFO("password=%s", cfg.password.empty() ? "(none)" : cfg.password.c_str());
		else {
			cfg.password = words[1];
			if (cfg.password.size() > 15) cfg.password.resize(15);
			r.broadcast_session_rules();
			LOG_INFO("password %s", cfg.password.empty() ? "cleared" : "updated");
		}
	} else if (cmd == "ban" && words.size() >= 2) {
		const auto key = cmp::sanitize_player_key(words[1], "player");
		bannedKeys.insert(key);
		persist_bans(bannedKeys);
		log_json_event(cfg.jsonLog, std::string("{\"event\":\"ban\",\"key\":\"") + key + "\"}");
		LOG_INFO("banned key=%s", key.c_str());
	} else if (cmd == "unban" && words.size() >= 2) {
		const auto key = cmp::sanitize_player_key(words[1], "player");
		bannedKeys.erase(key);
		persist_bans(bannedKeys);
		LOG_INFO("unbanned key=%s", key.c_str());
	} else if (cmd == "bans") {
		if (bannedKeys.empty()) LOG_INFO("bans: (none)");
		else for (const auto& key : bannedKeys) LOG_INFO("ban %s", key.c_str());
	} else if (cmd == "motd") {
		if (words.size() < 2) LOG_INFO("motd=%s", cfg.motd.c_str());
		else {
			cfg.motd.clear();
			for (std::size_t i = 1; i < words.size(); ++i) {
				if (i > 1) cfg.motd.push_back(' ');
				cfg.motd += words[i];
			}
			LOG_INFO("motd set to %s", cfg.motd.c_str());
		}
	} else LOG_WARN("unknown command: %s (type help)", words[0].c_str());
}
